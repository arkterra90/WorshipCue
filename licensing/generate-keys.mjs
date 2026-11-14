#!/usr/bin/env node
// Small-distribution administration. Real keys are issued by the Worker,
// never generated locally. See README.md for usage and partial-failure recovery.
import { chmod, mkdir, open, readFile, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { b64 } from "./worker.mjs";

const defaultOrigin = "https://worshipcue-licensing.jerqin.workers.dev";
const defaultSecrets = fileURLToPath(new URL("./secrets/", import.meta.url));
const help = `Issue WorshipCue license keys for a small distribution (Node.js 24+).

Usage:
  node licensing/generate-keys.mjs --batch first-church
  node licensing/generate-keys.mjs --batch pilot-group --count 5

Options:
  --batch NAME   Required unique batch name: letters, numbers, hyphens, underscores
  --count N      Number of licenses, 1–25 (default: 1); each supports five devices
  --url ORIGIN   HTTPS licensing service (default: ${defaultOrigin})
  --help         Show this help without issuing keys

Reads licensing/secrets/worker-secrets.json. Saves private records and one
recipient .txt file per key under licensing/secrets/issued/NAME/.
Existing batches are never overwritten or resumed. Keys are not printed.
`;

export async function generateKeys(
  { batch, count = 1, origin = defaultOrigin },
  { secretsDir = defaultSecrets, fetchImpl = fetch, log = console.log } = {},
) {
  if (typeof batch !== "string" || !/^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/.test(batch))
    throw Error("Supply --batch with 1–64 letters, numbers, hyphens or underscores; start with a letter or number.");
  if (!Number.isSafeInteger(count) || count < 1 || count > 25)
    throw Error("--count must be a whole number from 1 to 25.");
  let url;
  try {
    url = new URL(origin);
  } catch {
    throw Error("--url must be an HTTPS origin.");
  }
  if (url.protocol !== "https:" || url.username || url.password ||
      url.pathname !== "/" || url.search || url.hash)
    throw Error("--url must be an HTTPS origin without credentials, path, query or fragment.");

  let token;
  try {
    token = JSON.parse(await readFile(join(secretsDir, "worker-secrets.json"), "utf8")).ADMIN_TOKEN;
  } catch {
    throw Error("Cannot read licensing/secrets/worker-secrets.json. Restore the existing administrator credentials; do not regenerate service secrets.");
  }
  if (typeof token !== "string" || !token.trim())
    throw Error("Administrator credentials are missing ADMIN_TOKEN.");

  await mkdir(secretsDir, { recursive: true, mode: 0o700 });
  await chmod(secretsDir, 0o700);
  const issuedDir = join(secretsDir, "issued");
  await mkdir(issuedDir, { recursive: true, mode: 0o700 });
  await chmod(issuedDir, 0o700);
  const directory = join(issuedDir, batch);
  try {
    // Reserve the batch before any network request, including concurrent runs.
    await mkdir(directory, { mode: 0o700 });
  } catch (error) {
    if (error.code === "EEXIST")
      throw Error("That batch already exists. Check its records before choosing a new batch name; no keys were requested.");
    throw Error("Cannot create the private batch directory; no keys were requested.");
  }

  let completed = 0;
  const journal = await open(join(directory, "licenses.jsonl"), "wx", 0o600);
  const record = async (entry) => {
    await journal.writeFile(JSON.stringify(entry) + "\n");
    await journal.sync();
  };
  try {
    await record({ event: "batch", batch, count, origin: url.origin, createdAt: new Date().toISOString() });
    for (let index = 1; index <= count; index++) {
      // Persist intent first. A request without a matching issued record means
      // its outcome may be unknown; never automatically repeat issuance.
      await record({ event: "request", index, requestedAt: new Date().toISOString() });
      let response;
      try {
        response = await fetchImpl(new URL("/admin/issue", url), {
          method: "POST",
          redirect: "error",
          signal: AbortSignal.timeout(15000),
          headers: {
            Authorization: "Bearer " + token,
            "Content-Type": "application/json",
          },
          body: JSON.stringify({
            timestamp: Math.floor(Date.now() / 1000),
            nonce: b64(crypto.getRandomValues(new Uint8Array(24))),
          }),
        });
      } catch {
        throw Error("Connection failed or timed out; the last request may have issued a key.");
      }
      if (!response.ok)
        throw Error(`Service returned HTTP ${response.status}. For 429, wait at least a minute before requesting another batch.`);
      let license;
      try {
        license = await response.json();
      } catch {
        throw Error("Service returned an unreadable response; the last request may have issued a key.");
      }
      if (typeof license.licenseId !== "string" ||
          !/^[a-f0-9-]{36}$/i.test(license.licenseId) ||
          !/^WC-[A-Za-z0-9_-]{43}$/.test(license.code) ||
          license.maxActivations !== 5)
        throw Error("Service returned an unexpected license response; review the last request before issuing more keys.");
      await record({ event: "issued", index, licenseId: license.licenseId, code: license.code, maxActivations: license.maxActivations });
      completed++;
      const filename = `license-${String(index).padStart(3, "0")}.txt`;
      await writeFile(join(directory, filename),
        `WorshipCue license\n\nLicense code: ${license.code}\nLicense ID: ${license.licenseId}\n\n` +
        "To activate: open Settings → License Activation & Devices… in WorshipCue.\n" +
        "Enter the license code and a device name, then choose Activate while online.\n" +
        "This license supports five installations. Keep your code private.\n",
        { mode: 0o600, flag: "wx" });
      log(`Saved license ${index} of ${count}: ${filename}`);
    }
    await record({ event: "complete", issued: completed });
  } catch (error) {
    // Do not print raw exceptions: filesystem/fetch errors can contain secrets.
    const safeMessage = /^(Connection failed|Service returned)/.test(error.message)
      ? error.message : "Could not save the issuance records or recipient file.";
    throw Error(`Batch stopped with ${completed} license(s) recorded. ${safeMessage}\n` +
      `Review ${directory}/licenses.jsonl and keep all saved files. ` +
      "Do not repeat an uncertain request. A recorded key missing its .txt file can be recovered from the journal.");
  } finally {
    await journal.close();
  }
  log(`Issued ${completed} license(s). Private files: ${directory}`);
  return { directory, count: completed };
}

export async function main(args = process.argv.slice(2)) {
  const { values } = parseArgs({
    args,
    options: {
      batch: { type: "string" },
      count: { type: "string", default: "1" },
      url: { type: "string", default: defaultOrigin },
      help: { type: "boolean" },
    },
  });
  if (values.help) {
    console.log(help);
    return;
  }
  if (!/^\d+$/.test(values.count)) throw Error("--count must be a whole number from 1 to 25.");
  await generateKeys({ batch: values.batch, count: Number(values.count), origin: values.url });
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
