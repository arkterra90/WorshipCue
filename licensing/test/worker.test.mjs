import { test } from "node:test";
import assert from "node:assert/strict";
import { DatabaseSync } from "node:sqlite";
import { readFileSync, readdirSync } from "node:fs";
import worker, { b64, hash, signLease, unb64 } from "../worker.mjs";
// This adapter executes the production SQL against real SQLite, including the
// migration's triggers and transactional rollback. Worker crypto is unmodified.
class D1 {
  constructor() {
    this.db = new DatabaseSync(":memory:");
    for (const name of readdirSync(
      new URL("../migrations/", import.meta.url),
    ).sort())
      this.db.exec(
        readFileSync(new URL("../migrations/" + name, import.meta.url), "utf8"),
      );
  }
  withSession() {
    return this;
  }
  prepare(sql) {
    const db = this.db;
    let args = [];
    return {
      bind(...v) {
        args = v;
        return this;
      },
      async first() {
        return db.prepare(sql).get(...args) || null;
      },
      async all() {
        return { results: db.prepare(sql).all(...args) };
      },
      execute() {
        const statement = db.prepare(sql);
        return {
          results: statement.columns().length
            ? statement.all(...args)
            : (statement.run(...args), []),
        };
      },
    };
  }
  async batch(statements) {
    this.db.exec("BEGIN IMMEDIATE");
    try {
      const result = statements.map((s) => s.execute());
      this.db.exec("COMMIT");
      return result;
    } catch (e) {
      this.db.exec("ROLLBACK");
      throw e;
    }
  }
}
const enc = new TextEncoder();
async function environment() {
  const keys = await crypto.subtle.generateKey("Ed25519", true, [
    "sign",
    "verify",
  ]);
  return {
    DB: new D1(),
    LIMITER: {
      async limit() {
        return { success: true };
      },
    },
    CODE_PEPPER: "test-only-pepper",
    ADMIN_TOKEN: "test-only-admin",
    SIGNING_PRIVATE_KEY: b64(
      await crypto.subtle.exportKey("pkcs8", keys.privateKey),
    ),
    publicKey: keys.publicKey,
  };
}
async function device() {
  const keys = await crypto.subtle.generateKey("Ed25519", true, [
    "sign",
    "verify",
  ]);
  return {
    keys,
    publicKey: b64(await crypto.subtle.exportKey("raw", keys.publicKey)),
  };
}
function base() {
  return {
    timestamp: Math.floor(Date.now() / 1000),
    nonce: b64(crypto.getRandomValues(new Uint8Array(24))),
  };
}
async function call(env, path, body, admin = false) {
  return worker.fetch(
    new Request("https://license.example" + path, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        ...(admin ? { Authorization: "Bearer " + env.ADMIN_TOKEN } : {}),
      },
      body: JSON.stringify(body),
    }),
    env,
  );
}
async function signed(d, path, fields = {}) {
  const claims = { ...base(), ...fields, path, publicKey: d.publicKey };
  const payload = b64(enc.encode(JSON.stringify(claims)));
  return {
    ...base(),
    timestamp: claims.timestamp,
    nonce: claims.nonce,
    publicKey: d.publicKey,
    payload,
    signature: b64(
      await crypto.subtle.sign(
        "Ed25519",
        d.keys.privateKey,
        enc.encode(payload),
      ),
    ),
  };
}
async function issue(env) {
  const r = await call(env, "/admin/issue", base(), true);
  assert.equal(r.status, 201);
  return r.json();
}
test("five installs, duplicate activation, transfer, nonce replay and public-key ownership", async () => {
  const env = await environment(),
    license = await issue(env),
    devices = await Promise.all(Array.from({ length: 6 }, device));
  const requests = await Promise.all(
    devices.map((d, i) =>
      signed(d, "/v1/activate", { code: license.code, label: "Device " + i }),
    ),
  );
  const responses = await Promise.all(
    requests.map((b) => call(env, "/v1/activate", b)),
  );
  assert.deepEqual(
    responses.map((r) => r.status).sort(),
    [200, 200, 200, 200, 200, 409],
  );
  // Registration order is unspecified under concurrency. Normalize handles to
  // one active installation and the installation rejected at the limit.
  const active = responses.findIndex((r) => r.status === 200);
  for (const array of [devices, requests, responses])
    [array[0], array[active]] = [array[active], array[0]];
  const rejected = responses.findIndex((r) => r.status === 409);
  for (const array of [devices, requests, responses])
    [array[5], array[rejected]] = [array[rejected], array[5]];

  const first = await responses[0].json();
  assert(first.devices.length >= 1 && first.devices.length <= 5);
  const parts = first.lease.split(".");
  assert(
    await crypto.subtle.verify(
      "Ed25519",
      env.publicKey,
      unb64(parts[1]),
      enc.encode(parts[0]),
    ),
  );
  const lease = JSON.parse(new TextDecoder().decode(unb64(parts[0])));
  assert.equal(lease.exp - lease.iat, 604800);
  assert.equal(lease.installationId, await hash(unb64(devices[0].publicKey)));
  assert.equal((await call(env, "/v1/activate", requests[0])).status, 409);
  assert.equal(
    (
      await call(
        env,
        "/v1/activate",
        await signed(devices[0], "/v1/activate", {
          code: license.code,
          label: "Same",
        }),
      )
    ).status,
    200,
  );
  assert.equal(
    env.DB.db
      .prepare(
        "SELECT count(*) AS n FROM installations WHERE deactivated_at IS NULL",
      )
      .get().n,
    5,
  );
  let r = await call(
    env,
    "/v1/verify",
    await signed(devices[5], "/v1/verify", { licenseId: license.licenseId }),
  );
  assert.equal(r.status, 403);
  const deactivate = await signed(devices[0], "/v1/deactivate", {
    licenseId: license.licenseId,
  });
  assert.equal((await call(env, "/v1/deactivate", deactivate)).status, 200);
  assert.equal((await call(env, "/v1/activate", requests[5])).status, 200); // Failed activation rolled back its nonce.
  assert.equal((await call(env, "/v1/deactivate", deactivate)).status, 403);
  r = await call(
    env,
    "/v1/activate",
    await signed(devices[0], "/v1/activate", {
      code: license.code,
      label: "Reinstall",
    }),
  );
  assert.equal(r.status, 409);
  assert.equal(
    (
      await call(
        env,
        "/v1/verify",
        await signed(devices[0], "/v1/verify", {
          licenseId: license.licenseId,
        }),
      )
    ).status,
    403,
  );
});
test("revocation, invalid credentials, stale and tampered signatures, remote release", async () => {
  const env = await environment(),
    l = await issue(env),
    d = await device(),
    other = await device();
  assert.equal(
    (
      await call(
        env,
        "/v1/activate",
        await signed(d, "/v1/activate", {
          code: "WC-" + "a".repeat(43),
          label: "Wrong",
        }),
      )
    ).status,
    403,
  );
  const active = await signed(d, "/v1/activate", {
    code: l.code,
    label: "One",
  });
  assert.equal((await call(env, "/v1/activate", active)).status, 200);
  assert.equal(
    (
      await call(
        env,
        "/v1/activate",
        await signed(other, "/v1/activate", { code: l.code, label: "Two" }),
      )
    ).status,
    200,
  );
  const verify = await signed(d, "/v1/verify", { licenseId: l.licenseId });
  verify.signature = verify.signature.slice(0, -3) + "AAA";
  assert.equal((await call(env, "/v1/verify", verify)).status, 401);
  const stale = await signed(d, "/v1/verify", {
    licenseId: l.licenseId,
    timestamp: 1,
  });
  assert.equal((await call(env, "/v1/verify", stale)).status, 400);
  const release = await signed(d, "/v1/release", {
    licenseId: l.licenseId,
    target: await hash(unb64(other.publicKey)),
  });
  assert.equal((await call(env, "/v1/release", release)).status, 200);
  assert.equal((await call(env, "/v1/release", release)).status, 409);
  assert.equal(
    (
      await call(
        env,
        "/v1/verify",
        await signed(other, "/v1/verify", { licenseId: l.licenseId }),
      )
    ).status,
    403,
  );
  const revoke = await call(
    env,
    "/admin/revoke",
    { ...base(), licenseId: l.licenseId, reason: "Fixture revocation" },
    true,
  );
  assert.equal(revoke.status, 200);
  const r = await call(
    env,
    "/v1/verify",
    await signed(d, "/v1/verify", { licenseId: l.licenseId }),
  );
  assert.equal(r.status, 403);
  assert.equal((await r.json()).error, "license_revoked");
  assert.equal(
    (
      await call(
        env,
        "/v1/activate",
        await signed(d, "/v1/activate", { code: l.code, label: "Revoked" }),
      )
    ).status,
    403,
  );
});
test("HTTPS, admin authentication, issuance replay, rate limits, bounded inputs and verifier-only persistence", async () => {
  const env = await environment();
  let b = base();
  assert.equal((await call(env, "/admin/issue", b)).status, 401);
  const r = await call(env, "/admin/issue", b, true),
    l = await r.json();
  assert.equal(r.status, 201);
  assert.equal((await call(env, "/admin/issue", b, true)).status, 409);
  const row = env.DB.db.prepare("SELECT * FROM licenses").get();
  assert(!JSON.stringify(row).includes(l.code));
  assert.equal(row.max_activations, 5);
  assert.equal(
    (
      await worker.fetch(
        new Request("http://license.example", { method: "POST" }),
        env,
      )
    ).status,
    400,
  );
  assert.equal(
    (await call(env, "/v1/activate", { ...base(), padding: "x".repeat(9000) }))
      .status,
    413,
  );
  env.LIMITER = {
    async limit() {
      return { success: false };
    },
  };
  assert.equal((await call(env, "/admin/issue", base(), true)).status, 429);
});

test("concurrent activation of one installation is idempotent even at the cap", async () => {
  const env = await environment(),
    l = await issue(env),
    d = await device();
  const requests = await Promise.all(
    Array.from({ length: 5 }, () =>
      signed(d, "/v1/activate", { code: l.code, label: "One installation" }),
    ),
  );
  const results = await Promise.all(
    requests.map((r) => call(env, "/v1/activate", r)),
  );
  assert(results.every((r) => r.status === 200));
  assert.equal(
    env.DB.db
      .prepare(
        "SELECT count(*) AS n FROM installations WHERE deactivated_at IS NULL",
      )
      .get().n,
    1,
  );
  for (let i = 0; i < 4; i++) {
    const other = await device();
    assert.equal(
      (
        await call(
          env,
          "/v1/activate",
          await signed(other, "/v1/activate", { code: l.code, label: "Other" }),
        )
      ).status,
      200,
    );
  }
  assert.equal(
    (
      await call(
        env,
        "/v1/activate",
        await signed(d, "/v1/activate", { code: l.code, label: "Still one" }),
      )
    ).status,
    200,
  );
});

test("malformed signed envelopes fail without server errors", async () => {
  const env = await environment(),
    d = await device();
  let request = await signed(d, "/v1/verify", { licenseId: "unknown" });
  request.signature = "!";
  assert.equal((await call(env, "/v1/verify", request)).status, 400);
  request = await signed(d, "/v1/verify", { licenseId: "unknown" });
  request.payload = b64(enc.encode("null"));
  assert.equal((await call(env, "/v1/verify", request)).status, 400);
});
