# Troubleshooting

## Application cannot launch

Use the complete matching application package. On macOS, copy the app from its disk image to Applications before launching. Keep the app bundle intact; moving only its executable or MCP companion can break runtime loading. For missing Qt/platform-plugin/SQLite/TLS errors, reinstall the complete package or contact the person who supplied the build.

Unsigned development packages are labelled in their filename and window title. Signed/notarized release acceptance and Windows installers are still undergoing validation; use an approved build for your installation.

## Audience screen is blank or missing

Check the route in **Screens…**, confirm that the display is connected, and inspect the output status. Use **Windowed Preview** to inspect the route on the operator display. Choose **Restore Audience** to clear Blackout or Clear Text, then take the required slide live.

An unavailable saved physical mode hides that output. Choose a mode offered by the connected display or **Keep OS desktop mode**. Each fullscreen output requires a separate display. Ambiguous monitor identities may need reassignment.

## Navigation keys affect the wrong window

Click the operator's slide workspace and choose **Show** mode. Text fields and modal dialogs receive their own input. In Help, Space and arrows operate the reader or topic list. Click the operator window to resume live keyboard navigation.

## Lyrics or formatting look wrong

Confirm the selected source page and saved arrangement. Use **Edit Slide Text…** for the shared page, then Save. For overflow, use **Looks / Reflow…** and review a split or Fit Text. Take the saved slide live to capture appearance and external MCP changes.

## Media is missing or cannot play

Check the managed path and metadata in **Media Library** and try **Preview…**. For audio/video, start **Play Preview**. Restore missing managed files from a library backup, or import a supported replacement and update the service. Available codecs depend on Qt Multimedia and the OS. Check live mute and Blackout when audio is silent.

## MIDI does not trigger

In **Settings → MIDI Devices / Mappings…**, check that MIDI and the source/destination are enabled, apply device settings, and refresh devices. Review **Activity** and input learning. Verify source, channel, message type, number, value, target, and occurrence. Use Show mode and close modal dialogs and popup menus. Release a held note or change a repeated CC value to rearm the trigger.

For returned output messages, use distinct input/output channels or numbers and review echo suppression. Use **Release WorshipCue Notes** if owned notes remain sounding.

## MCP cannot connect or saved content is missing

Use **Settings → AI / MCP Setup… → Test Connection**. Missing/outdated companions require the complete matching application build. Check the executable and exact library path in the generated configuration, then reconnect the client. A Qt/SQLite deployment error requires the companion's runtime dependencies.

For new saved services, close modal editing and allow the automatic library refresh, then choose the saved service. After a lost save response, inspect existing services before retrying a creation call. Planning Center errors require account credentials, permission, and internet access.

## Cleanup finds little content or retains files

Enable **Show protected / unknown-age items** and read the reason. Unknown usage history and active/shared references stay protected. Moving to Recovery retains disk storage. Review a separate permanent removal to reclaim unshared files. Restore a batch and rescan if its content was used or changed after archiving.

[Audience and stage displays](outputs-stage.md) · [MIDI configuration](midi.md) · [MCP configuration and usage](mcp.md) · [Storage management and Recovery](storage.md)
