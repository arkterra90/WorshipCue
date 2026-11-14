// Usage: node admin.mjs issue https://worker.example [output-file]
//        node admin.mjs revoke https://worker.example LICENSE_ID REASON
import { readFile, writeFile, access } from "node:fs/promises";
import { b64 } from "./worker.mjs";
const [action, origin, ...args] = process.argv.slice(2);
if (!["issue", "revoke"].includes(action) || !origin)
  throw Error(
    "Use issue HTTPS_ORIGIN [OUTPUT_FILE] or revoke HTTPS_ORIGIN LICENSE_ID REASON",
  );
const url = new URL(origin);
if (
  url.protocol !== "https:" ||
  url.username ||
  url.password ||
  url.pathname !== "/" ||
  url.search ||
  url.hash
)
  throw Error("Use an HTTPS origin");
const target =
  args[0] || new URL("./secrets/developer-license.json", import.meta.url);
if (action === "issue") {
  try {
    await access(target);
    throw Error("Output already exists; refusing to issue an extra license.");
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
}
const { ADMIN_TOKEN } = JSON.parse(
  await readFile(
    new URL("./secrets/worker-secrets.json", import.meta.url),
    "utf8",
  ),
);
const body = {
  timestamp: Math.floor(Date.now() / 1000),
  nonce: b64(crypto.getRandomValues(new Uint8Array(24))),
  ...(action === "revoke" ? { licenseId: args[0], reason: args[1] } : {}),
};
const response = await fetch(new URL("/admin/" + action, url), {
  method: "POST",
  redirect: "error",
  headers: {
    Authorization: "Bearer " + ADMIN_TOKEN,
    "Content-Type": "application/json",
  },
  body: JSON.stringify(body),
});
if (!response.ok)
  throw Error(
    "Licensing administration failed (HTTP " +
      response.status +
      "). No credentials logged.",
  );
const result = await response.json();
if (action === "issue") {
  await writeFile(target, JSON.stringify(result, null, 2) + "\n", {
    mode: 0o600,
    flag: "wx",
  });
  console.log(
    "Issued one real license. Credential saved to protected local output; not printed.",
  );
} else
  console.log(
    "Revocation recorded by the backend. Existing offline leases expire within seven days.",
  );
