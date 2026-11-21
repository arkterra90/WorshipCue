import { b64, hash } from "./worker.mjs";

const encoder = new TextEncoder();
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
const recoverySeconds = 15 * 60;

export async function webLicense(env, licenseId, create) {
  if (typeof licenseId !== "string" || !uuid.test(licenseId) || !env.CODE_PEPPER) return null;

  // Derive the same high-entropy code for a retried request without storing it.
  const key = await crypto.subtle.importKey(
    "raw", encoder.encode(env.CODE_PEPPER), { name: "HMAC", hash: "SHA-256" }, false, ["sign"],
  );
  const code = "WC-" + b64(await crypto.subtle.sign(
    "HMAC", key, encoder.encode("worshipcue.website.issue.v1:" + licenseId),
  ));
  const verifier = await hash(env.CODE_PEPPER + ":" + code);
  const now = Math.floor(Date.now() / 1000);
  const db = env.DB.withSession("first-primary");
  const read = db.prepare(`
    SELECT max_activations FROM licenses
    WHERE id=? AND code_verifier=? AND status='active' AND issued_at>=?
  `).bind(licenseId, verifier, now - recoverySeconds);
  let license;
  if (create) {
    const [, result] = await db.batch([
      db.prepare(`
        INSERT INTO licenses(id, code_verifier, issued_at) VALUES(?,?,?)
        ON CONFLICT(id) DO NOTHING
      `).bind(licenseId, verifier, now),
      read,
    ]);
    [license] = result.results;
  } else {
    license = await read.first();
  }
  return license ? { licenseId, code, maxActivations: license.max_activations } : null;
}
