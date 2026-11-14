// Creates local secrets without writing credentials to stdout. Upload this file
// with `wrangler secret bulk secrets/worker-secrets.json` from this directory.
import { mkdir, writeFile, chmod } from "node:fs/promises";
import { b64 } from "./worker.mjs";
const dir = new URL("./secrets/", import.meta.url);
await mkdir(dir, { recursive: true, mode: 0o700 });
await chmod(dir, 0o700);
const keys = await crypto.subtle.generateKey("Ed25519", true, [
  "sign",
  "verify",
]);
const secrets = {
  SIGNING_PRIVATE_KEY: b64(
    await crypto.subtle.exportKey("pkcs8", keys.privateKey),
  ),
  CODE_PEPPER: b64(crypto.getRandomValues(new Uint8Array(32))),
  ADMIN_TOKEN: b64(crypto.getRandomValues(new Uint8Array(32))),
};
await writeFile(
  new URL("worker-secrets.json", dir),
  JSON.stringify(secrets, null, 2) + "\n",
  { mode: 0o600, flag: "wx" },
);
await writeFile(
  new URL("public-key.txt", dir),
  b64(await crypto.subtle.exportKey("raw", keys.publicKey)) + "\n",
  { mode: 0o600, flag: "wx" },
);
console.log(
  "Created protected local secret files. No credentials were printed.",
);
