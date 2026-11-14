import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { generateKeys } from "../generate-keys.mjs";

const license = (index) => ({
  licenseId: `00000000-0000-4000-8000-${String(index).padStart(12, "0")}`,
  code: "WC-" + String(index).repeat(43),
  maxActivations: 5,
});

async function setup(t, fetchImpl) {
  const secretsDir = await mkdtemp(join(tmpdir(), "worshipcue-issuance-"));
  t.after(() => rm(secretsDir, { recursive: true, force: true }));
  await writeFile(join(secretsDir, "worker-secrets.json"), JSON.stringify({ ADMIN_TOKEN: "fixture-admin-token" }));
  const logs = [];
  return { secretsDir, fetchImpl, log: (line) => logs.push(line), logs };
}

async function records(directory) {
  return (await readFile(join(directory, "licenses.jsonl"), "utf8"))
    .trim().split("\n").map((line) => JSON.parse(line));
}

test("issues separate licenses, saves distribution files and private records without logging credentials", async (t) => {
  const requests = [];
  const deps = await setup(t, async (url, options) => {
    requests.push({ url, options });
    return Response.json(license(requests.length), { status: 201 });
  });
  const { directory, count } = await generateKeys({ batch: "pilot-group", count: 3 }, deps);
  assert.equal(count, 3);
  const journal = await records(directory);
  assert.deepEqual(journal.filter((r) => r.event === "issued").map((r) => r.code),
    [license(1).code, license(2).code, license(3).code]);
  assert.equal(journal.at(-1).event, "complete");
  const nonces = new Set();
  for (const [i, { url, options }] of requests.entries()) {
    assert.equal(url.href, "https://worshipcue-licensing.jerqin.workers.dev/admin/issue");
    assert.equal(options.method, "POST");
    assert.equal(options.redirect, "error");
    assert.equal(options.headers.Authorization, "Bearer fixture-admin-token");
    const body = JSON.parse(options.body);
    assert.ok(Math.abs(Date.now() / 1000 - body.timestamp) < 5);
    assert.match(body.nonce, /^[A-Za-z0-9_-]{32}$/);
    nonces.add(body.nonce);
    const recipient = join(directory, `license-00${i + 1}.txt`);
    const content = await readFile(recipient, "utf8");
    assert.ok(content.includes(license(i + 1).code));
    assert.ok(content.includes("License Activation & Devices"));
    assert.ok(content.includes("five installations"));
    assert.ok(!content.includes("fixture-admin-token"));
    if (process.platform !== "win32")
      assert.equal((await stat(recipient)).mode & 0o777, 0o600);
  }
  assert.equal(nonces.size, 3);
  assert.ok(!deps.logs.join("\n").includes("WC-"));
  assert.ok(!deps.logs.join("\n").includes("fixture-admin-token"));
  if (process.platform !== "win32") {
    assert.equal((await stat(directory)).mode & 0o777, 0o700);
    assert.equal((await stat(join(directory, "licenses.jsonl"))).mode & 0o777, 0o600);
  }
});

test("refuses existing or concurrent batches without requesting duplicate licenses", async (t) => {
  let calls = 0;
  const deps = await setup(t, async () => Response.json(license(++calls)));
  const options = { batch: "one-recipient" };
  const results = await Promise.allSettled([generateKeys(options, deps), generateKeys(options, deps)]);
  assert.equal(results.filter((r) => r.status === "fulfilled").length, 1);
  assert.equal(results.filter((r) => r.status === "rejected").length, 1);
  assert.equal(calls, 1);
  await assert.rejects(generateKeys(options, deps), /batch already exists/);
  assert.equal(calls, 1);
});

test("validates batch, quantity, origin and credentials before requesting licenses", async (t) => {
  let calls = 0;
  const deps = await setup(t, async () => { calls++; throw Error("unexpected request"); });
  for (const options of [
    {}, { batch: "../escape" }, { batch: "" }, { batch: "a/b" },
    { batch: "pilot", count: 0 }, { batch: "pilot", count: 26 },
    { batch: "pilot", count: 1.5 },
    { batch: "pilot", origin: "http://example.com" },
    { batch: "pilot", origin: "https://user:pass@example.com" },
    { batch: "pilot", origin: "https://example.com/path" },
    { batch: "pilot", origin: "https://example.com?token=private" },
  ]) await assert.rejects(generateKeys(options, deps));
  await writeFile(join(deps.secretsDir, "worker-secrets.json"), "{}");
  await assert.rejects(generateKeys({ batch: "pilot" }, deps), /missing ADMIN_TOKEN/);
  assert.equal(calls, 0);
  assert.deepEqual(await readdir(deps.secretsDir), ["worker-secrets.json"]);
});

test("preserves earlier keys and records an uncertain request without retrying or leaking errors", async (t) => {
  let calls = 0;
  const deps = await setup(t, async () => {
    if (++calls === 2) throw Error("failure containing fixture-admin-token");
    return Response.json(license(calls));
  });
  await assert.rejects(generateKeys({ batch: "partial", count: 3 }, deps), (error) => {
    assert.match(error.message, /1 license\(s\) recorded/);
    assert.match(error.message, /last request may have issued a key/);
    assert.ok(!error.message.includes("fixture-admin-token"));
    return true;
  });
  assert.equal(calls, 2);
  const directory = join(deps.secretsDir, "issued", "partial");
  const journal = await records(directory);
  assert.equal(journal.filter((r) => r.event === "request").length, 2);
  assert.equal(journal.filter((r) => r.event === "issued").length, 1);
  assert.equal(journal.at(-1).event, "request");
  assert.ok((await readFile(join(directory, "license-001.txt"), "utf8")).includes(license(1).code));
  await assert.rejects(generateKeys({ batch: "partial" }, deps), /already exists/);
  assert.equal(calls, 2);
});

test("stops on service rejection or malformed success, without logging response contents", async (t) => {
  for (const [batch, reply] of [
    ["unauthorized", () => Response.json({ error: "fixture-admin-token" }, { status: 401 })],
    ["limited", () => Response.json({}, { status: 429 })],
    ["invalid-json", () => new Response("fixture-admin-token", { status: 201 })],
    ["invalid-license", () => Response.json({ code: "fixture-admin-token" })],
  ]) {
    let calls = 0;
    const deps = await setup(t, async () => { calls++; return reply(); });
    await assert.rejects(generateKeys({ batch, count: 2 }, deps), (error) => {
      assert.match(error.message, /0 license\(s\) recorded/);
      assert.ok(!error.message.includes("fixture-admin-token"));
      return true;
    });
    assert.equal(calls, 1);
    assert.ok(!deps.logs.join("\n").includes("fixture-admin-token"));
  }
});
