// Opt-in integration check against the real deployed Worker. Temporary
// installations are deactivated in finally; an existing native activation stays.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { b64, unb64 } from "./worker.mjs";
const origin = process.argv[2];
if (!origin || new URL(origin).protocol !== "https:")
  throw Error("Supply the deployed HTTPS origin");
const license = JSON.parse(
  await readFile(
    new URL("./secrets/developer-license.json", import.meta.url),
    "utf8",
  ),
);
const enc = new TextEncoder();
const devices = await Promise.all(
  Array.from({ length: 6 }, async () => {
    const keys = await crypto.subtle.generateKey("Ed25519", true, [
      "sign",
      "verify",
    ]);
    return {
      keys,
      publicKey: b64(await crypto.subtle.exportKey("raw", keys.publicKey)),
      active: false,
    };
  }),
);
async function request(device, path) {
  const payloadObject = {
    path,
    timestamp: Math.floor(Date.now() / 1000),
    nonce: b64(crypto.getRandomValues(new Uint8Array(24))),
    publicKey: device.publicKey,
    licenseId: license.licenseId,
    ...(path === "/v1/activate"
      ? { code: license.code, label: "Temporary licensing integration check" }
      : {}),
  };
  const payload = b64(enc.encode(JSON.stringify(payloadObject)));
  const response = await fetch(new URL(path, origin), {
    method: "POST",
    redirect: "error",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      ...payloadObject,
      code: undefined,
      label: undefined,
      payload,
      signature: b64(
        await crypto.subtle.sign(
          "Ed25519",
          device.keys.privateKey,
          enc.encode(payload),
        ),
      ),
    }),
  });
  const body = await response.json();
  if (response.status === 200 && path === "/v1/activate") device.active = true;
  if (response.status === 200 && path === "/v1/deactivate")
    device.active = false;
  return { status: response.status, body };
}
try {
  const results = await Promise.all(
    devices.map((d) => request(d, "/v1/activate")),
  );
  const active = devices.filter((d) => d.active),
    denied = devices.filter(
      (_, i) =>
        results[i].status === 409 &&
        results[i].body.error === "activation_limit",
    );
  assert(
    active.length > 0 &&
      active.length <= 5 &&
      denied.length > 0 &&
      active.length + denied.length === 6,
  );
  const first = await request(active[0], "/v1/verify");
  assert.equal(first.status, 200);
  assert.equal(first.body.devices.length, 5);
  assert.equal((await request(active[0], "/v1/activate")).status, 200);
  assert.equal((await request(active[0], "/v1/deactivate")).status, 200);
  assert.equal((await request(denied[0], "/v1/activate")).status, 200);
  console.log(
    "Real Worker/D1 checks passed: five-machine cap under concurrency, duplicate registration, verification, deactivation and transfer.",
  );
} finally {
  let failed = false;
  for (const d of devices.filter((d) => d.active)) {
    const r = await request(d, "/v1/deactivate");
    if (r.status !== 200) failed = true;
  }
  if (failed)
    throw Error(
      "Temporary device cleanup needs review in License Activation & Devices. No credentials logged.",
    );
  console.log("Temporary integration activations deactivated.");
}
