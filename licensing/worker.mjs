const enc = new TextEncoder();
export const b64 = (bytes) =>
  btoa(String.fromCharCode(...new Uint8Array(bytes)))
    .replaceAll("+", "-")
    .replaceAll("/", "_")
    .replaceAll("=", "");
export const unb64 = (text) =>
  Uint8Array.from(atob(text.replaceAll("-", "+").replaceAll("_", "/")), (c) =>
    c.charCodeAt(0),
  );
export const hash = async (text) =>
  b64(
    await crypto.subtle.digest(
      "SHA-256",
      typeof text === "string" ? enc.encode(text) : text,
    ),
  );
const fail = (code, status = 400) => {
  throw Object.assign(new Error(code), { code, status });
};
const json = (data, status = 200) =>
  Response.json(data, {
    status,
    headers: {
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff",
    },
  });
const validText = (s, max) =>
  typeof s === "string" && s.length > 0 && s.length <= max;
export async function signLease(env, licenseId, installationId, now) {
  const key = await crypto.subtle.importKey(
    "pkcs8",
    unb64(env.SIGNING_PRIVATE_KEY),
    "Ed25519",
    false,
    ["sign"],
  );
  const payload = b64(
    enc.encode(
      JSON.stringify({
        v: 1,
        aud: "worshipcue-live",
        licenseId,
        installationId,
        iat: now,
        exp: now + 7 * 86400,
        status: "active",
      }),
    ),
  );
  return (
    payload +
    "." +
    b64(await crypto.subtle.sign("Ed25519", key, enc.encode(payload)))
  );
}
export default {
  async fetch(request, env) {
    try {
      if (!env.SIGNING_PRIVATE_KEY || !env.CODE_PEPPER || !env.ADMIN_TOKEN)
        fail("service_unavailable", 503);
      const url = new URL(request.url),
        path = url.pathname;
      if (url.protocol !== "https:") fail("https_required", 400);
      if (request.method !== "POST") fail("method_not_allowed", 405);
      if (
        !env.LIMITER ||
        !(
          await env.LIMITER.limit({
            key: request.headers.get("CF-Connecting-IP") || "unknown",
          })
        ).success
      )
        fail("rate_limited", 429);
      if (!request.headers.get("content-type")?.startsWith("application/json"))
        fail("invalid_content_type");
      // Bound actual streamed bytes, including chunked requests without Content-Length.
      const reader = request.body?.getReader();
      if (!reader) fail("invalid_request");
      let bytes = 0,
        chunks = [];
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        bytes += value.length;
        if (bytes > 8192) {
          await reader.cancel();
          fail("request_too_large", 413);
        }
        chunks.push(value);
      }
      const data = new Uint8Array(bytes);
      let offset = 0;
      for (const c of chunks) {
        data.set(c, offset);
        offset += c.length;
      }
      let body;
      try {
        body = JSON.parse(new TextDecoder().decode(data));
      } catch {
        fail("invalid_json");
      }
      const now = Math.floor(Date.now() / 1000);
      if (
        !body ||
        !Number.isSafeInteger(body.timestamp) ||
        Math.abs(now - body.timestamp) > 300 ||
        !/^[A-Za-z0-9_-]{22,64}$/.test(body.nonce || "")
      )
        fail("stale_request");
      // D1 reads/writes for each request use primary-consistent sessions.
      const db = env.DB.withSession("first-primary");
      const stmt = (sql, ...args) => db.prepare(sql).bind(...args);
      const nonce = (actor) =>
        stmt(
          "INSERT INTO request_nonces(actor,nonce,expires_at) VALUES(?,?,?)",
          actor,
          body.nonce,
          now + 600,
        );
      const cleanup = stmt(
        "DELETE FROM request_nonces WHERE expires_at < ?",
        now,
      );
      if (path === "/admin/issue" || path === "/admin/revoke") {
        const token = request.headers.get("Authorization") || "";
        if (
          !env.ADMIN_TOKEN ||
          (await hash(token)) !== (await hash("Bearer " + env.ADMIN_TOKEN))
        )
          fail("unauthorized", 401);
        if (path === "/admin/issue") {
          // A high-entropy credential is generated only here, never supplied by the client.
          const code = "WC-" + b64(crypto.getRandomValues(new Uint8Array(32))),
            id = crypto.randomUUID();
          await db.batch([
            cleanup,
            nonce("admin"),
            stmt(
              "INSERT INTO licenses(id,code_verifier,issued_at) VALUES(?,?,?)",
              id,
              await hash(env.CODE_PEPPER + ":" + code),
              now,
            ),
          ]);
          return json({ licenseId: id, code, maxActivations: 5 }, 201);
        }
        if (!validText(body.licenseId, 64) || !validText(body.reason, 200))
          fail("invalid_request");
        await db.batch([
          cleanup,
          nonce("admin"),
          stmt(
            "UPDATE licenses SET status='revoked',revoked_at=?,revocation_reason=? WHERE id=?",
            now,
            body.reason,
            body.licenseId,
          ),
        ]);
        return json({ ok: true });
      }
      if (
        ![
          "/v1/activate",
          "/v1/verify",
          "/v1/deactivate",
          "/v1/devices",
          "/v1/release",
        ].includes(path)
      )
        fail("not_found", 404);
      if (
        !/^[A-Za-z0-9_-]{43}$/.test(body.publicKey || "") ||
        !/^[A-Za-z0-9_-]{86}$/.test(body.signature || "")
      )
        fail("invalid_request");
      let key;
      try {
        key = await crypto.subtle.importKey(
          "raw",
          unb64(body.publicKey),
          "Ed25519",
          false,
          ["verify"],
        );
      } catch {
        fail("invalid_request");
      }
      // Sign exactly the encoded JSON, avoiding cross-language canonicalization differences.
      if (!validText(body.payload, 4096)) fail("invalid_request");
      let signed;
      try {
        signed = JSON.parse(new TextDecoder().decode(unb64(body.payload)));
      } catch {
        fail("invalid_request");
      }
      if (!signed || typeof signed !== "object" || Array.isArray(signed))
        fail("invalid_request");
      if (
        signed.path !== path ||
        signed.nonce !== body.nonce ||
        signed.timestamp !== body.timestamp ||
        signed.publicKey !== body.publicKey
      )
        fail("invalid_signature", 401);
      if (
        !(await crypto.subtle.verify(
          "Ed25519",
          key,
          unb64(body.signature),
          enc.encode(body.payload),
        ))
      )
        fail("invalid_signature", 401);
      const installationId = await hash(unb64(body.publicKey));
      let license;
      if (path === "/v1/activate") {
        if (
          !/^WC-[A-Za-z0-9_-]{43}$/.test(signed.code || "") ||
          !validText(signed.label, 80)
        )
          fail("invalid_request");
        license = await stmt(
          "SELECT * FROM licenses WHERE code_verifier=?",
          await hash(env.CODE_PEPPER + ":" + signed.code),
        ).first();
      } else {
        if (!validText(signed.licenseId, 64)) fail("invalid_request");
        license = await stmt(
          "SELECT * FROM licenses WHERE id=?",
          signed.licenseId,
        ).first();
      }
      if (!license) fail("invalid_license", 403);
      if (license.status !== "active") fail("license_revoked", 403);
      const id = license.id;
      const actor = id + ":" + installationId;
      const existing = await stmt(
        "SELECT * FROM installations WHERE license_id=? AND installation_id=?",
        id,
        installationId,
      ).first();
      if (
        path !== "/v1/activate" &&
        (!existing || existing.deactivated_at !== null)
      )
        fail("installation_deactivated", 403);
      // Every mutation is in the same transaction as nonce consumption and a fresh
      // status check. The trigger also protects concurrent sixth registrations.
      const guard = stmt(
        "INSERT INTO request_nonces(actor,nonce,expires_at) SELECT ?,?,? WHERE EXISTS(SELECT 1 FROM licenses WHERE id=? AND status='active')",
        actor,
        body.nonce,
        now + 600,
        id,
      );
      // A later SELECT detects revocation/deactivation races before returning a lease.
      const actions = [cleanup, guard];
      if (path === "/v1/activate") {
        actions.push(
          stmt(
            "INSERT INTO installations(license_id,installation_id,public_key,label,activated_at,verified_at) SELECT ?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM licenses WHERE id=? AND status='active') ON CONFLICT(license_id,installation_id) DO UPDATE SET deactivated_at=NULL,verified_at=excluded.verified_at,label=excluded.label",
            id,
            installationId,
            body.publicKey,
            signed.label,
            now,
            now,
            id,
          ),
        );
      } else if (path === "/v1/deactivate" || path === "/v1/release") {
        const target =
          path === "/v1/deactivate" ? installationId : signed.target;
        if (!/^[A-Za-z0-9_-]{43}$/.test(target || "")) fail("invalid_request");
        actions.push(
          stmt(
            "UPDATE installations SET deactivated_at=? WHERE license_id=? AND installation_id=? AND EXISTS(SELECT 1 FROM installations a JOIN licenses l ON l.id=a.license_id WHERE a.license_id=? AND a.installation_id=? AND a.deactivated_at IS NULL AND l.status='active')",
            now,
            id,
            target,
            id,
            installationId,
          ),
        );
      } else
        actions.push(
          stmt(
            "UPDATE installations SET verified_at=? WHERE license_id=? AND installation_id=? AND deactivated_at IS NULL",
            now,
            id,
            installationId,
          ),
        );
      actions.push(stmt("SELECT status FROM licenses WHERE id=?", id));
      actions.push(
        stmt(
          "SELECT deactivated_at FROM installations WHERE license_id=? AND installation_id=?",
          id,
          installationId,
        ),
      );
      const results = await db.batch(actions);
      if (results.at(-2).results[0]?.status !== "active")
        fail("license_revoked", 403);
      if (path === "/v1/deactivate") return json({ ok: true });
      if (results.at(-1).results[0]?.deactivated_at !== null)
        fail("installation_deactivated", 403);
      if (path === "/v1/release") return json({ ok: true });
      const devices = (
        await stmt(
          "SELECT installation_id AS id,label,activated_at AS activatedAt,verified_at AS verifiedAt FROM installations WHERE license_id=? AND deactivated_at IS NULL ORDER BY activated_at",
          id,
        ).all()
      ).results;
      return json({
        lease: await signLease(env, id, installationId, now),
        devices,
      });
    } catch (error) {
      const msg = String(error.message || "");
      if (msg.includes("activation_limit"))
        return json({ error: "activation_limit" }, 409);
      if (msg.includes("request_nonces"))
        return json({ error: "replayed_request" }, 409);
      if (msg.includes("license_revoked"))
        return json({ error: "license_revoked" }, 403);
      // Do not log request bodies, credentials, SQL arguments, or exception internals.
      return json(
        { error: error.code || "service_unavailable" },
        error.status || 503,
      );
    }
  },
};
