# Generate license keys

Use Node.js 24 or newer. Run these commands from the repository root; the script
also works from another directory when invoked by its full path. It uses the
existing administrator credentials in `licensing/secrets/worker-secrets.json` and
the deployed WorshipCue licensing service. No Cloudflare login is needed.

Generate one license for a recipient:

```sh
node licensing/generate-keys.mjs --batch first-church
```

Generate five separate licenses for a small distribution:

```sh
node licensing/generate-keys.mjs --batch pilot-group --count 5
```

Choose a new batch name each time. Each license allows **five installations**;
`--count 5` issues five separate licenses, not five device slots on one license.
The script accepts 1–25 licenses per batch. Run `--help` for options, including
`--url` for a separate licensing environment with matching admin credentials.

Files are saved under `licensing/secrets/issued/<batch>/`:

- `license-001.txt`, `license-002.txt`, etc.: send one file privately to each
  recipient. Each includes the code, license ID, and activation instructions.
- `licenses.jsonl`: private issuance journal containing the keys and their IDs.
  Keep it as your distribution record; record recipients separately if desired.

The directory is excluded from Git. On macOS/Linux directories are owner-only
and files are readable/writable only by the owner. On Windows, keep the repository
in your private user profile with appropriate account permissions. Codes and
administrator credentials are not printed. Keep an encrypted backup of the records.
Only share the recipient file, never `worker-secrets.json` or the full journal.

Existing batch names are refused before issuing any keys, including simultaneous
runs. Every returned key is saved to the journal before its recipient file is
written. Issuance stops on the first error and does not automatically retry.
Preserve a partial batch: already-recorded keys remain usable, and a missing
recipient file can be recreated from its `issued` journal entry. A `request` entry
without an `issued` entry can mean a key was issued but its response was lost;
review the backend issuance records before requesting a replacement. Plaintext
codes cannot be recovered from the server. Do not rerun uncertain requests blindly.
For HTTP 429, wait at least a minute before making further requests; earlier
requests from the same IP also count toward the service's rate limit.

To revoke a license, use its ID from the recipient file or journal:

```sh
node licensing/admin.mjs revoke https://worshipcue-licensing.jerqin.workers.dev LICENSE_ID "Reason for revocation"
```

Revoked devices lose authorization on their next successful verification or
within seven days when offline. The original single-license `admin.mjs issue`
command remains available. See [operations and deployment](../docs/LICENSING.md)
for backend details. This script issues real licenses but does not send email.
