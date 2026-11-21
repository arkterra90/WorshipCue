# License activation and devices

WorshipCue always supports editing: manage the Content and Media Libraries, create service playlists, edit songs and sermons, and save presentations without a license. Selected-slide previews, media inspection, output configuration, and stage layout design remain available. Unlicensed startup selects **Edit** mode so slide selection continues to work as an editor.

A valid license enables live audience and stage windows, including **Windowed Preview**, taking slides and service items live, live media playback, and live MIDI control. The status bar shows **Licensed** or **Editing only — activate to present**. Choosing Take Live without authorization opens the activation window.

## Activate this machine

1. Open **Settings → License Activation & Devices…**, or choose **License…** in the status bar.
2. Enter your license code and a recognizable **Device name**. That name is sent to the licensing service; use a generic name if preferred.
3. Choose **Activate** while connected to the internet. WorshipCue remembers the validated activation, so you do not need to enter the code or your computer login password each time you open the app. The window shows the authorization expiry and active devices after a successful response.

Each license allows five installations. Repeated activation or verification of the same installation uses one slot. If all five slots are occupied, deactivate an old installation before activating another.

## Offline use

The service issues a signed authorization valid for seven days. WorshipCue checks at startup and attempts daily renewal while running. A valid cached authorization supports live operation during temporary outages. Failed connections retry hourly while offline authorization remains valid, or every five minutes if it is invalid. **Verify / Refresh Devices** retries immediately.

If authorization expires or the server reports revocation/deactivation, WorshipCue enters editing-only mode. Live content and media stop; output windows close. Output assignments and your library remain saved. Renew or activate before returning to live operation. Keep the computer's clock accurate; significant backward clock changes invalidate offline authorization.

## Deactivate or transfer

Use **Deactivate This Machine…** and confirm before transferring to another machine. Online confirmation is required to release the activation. This machine then enters editing-only mode.

To release an unavailable machine, select its row and choose **Deactivate Selected Device…** from another active installation on the license. Then activate the replacement machine. A remote device may retain its existing offline authorization for up to seven days; revocation and remote deactivation take effect at its next successful verification or lease expiry.

## Reinstalls, hardware changes, and privacy

The installation identity and signed authorization are saved automatically in an encrypted, private activation file on macOS, or protected with Windows DPAPI for the current user. Startup and background verification do not request your computer login password. Copying the library or presentation files does not copy the license. A reinstall preserves the installation when this protected state survives. Removing that state, switching OS users, or replacing hardware may require entering the activation code again; release the old installation from another licensed machine. If protected state is unreadable or bound to different hardware, contact the license administrator for recovery before removing it.

Older Keychain-based activations migrate silently when macOS permits access without a prompt. If they cannot be migrated, enter your activation code once in the updated app and release the older installation if needed. Subsequent launches and app updates reuse the saved activation.

If a cache write temporarily fails, WorshipCue retries while honoring an existing valid saved authorization for up to five minutes. Lease expiry, revocation, and device binding still apply. The License panel shows when the app is retrying.

The service stores a random installation public-key identifier, your chosen device name, and activation/verification times. The hardware-binding hash stays on this computer. License codes are not retained by the desktop after activation. Treat your code as private and keep a secure backup outside the library.

If activation cannot connect, check the internet connection, computer clock, and licensing service availability. A build reporting that its licensing service is unconfigured needs an administrator to configure its HTTPS endpoint and public verification key. Editing remains available if activation or protected storage fails.

[Getting started](getting-started.md) · [Audience and stage displays](outputs-stage.md) · [MIDI configuration](midi.md) · [Troubleshooting](troubleshooting.md)
