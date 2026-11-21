import { WorkerEntrypoint } from "cloudflare:workers";
import worker from "./worker.mjs";
import { webLicense } from "./web-issuance.mjs";

export default worker;

// Callable only through an explicit Worker service binding.
export class WebsiteIssuer extends WorkerEntrypoint {
  issue(licenseId) {
    return webLicense(this.env, licenseId, true);
  }

  recover(licenseId) {
    return webLicense(this.env, licenseId, false);
  }
}
