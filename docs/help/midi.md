# MIDI configuration

Open **Settings → MIDI Devices / Mappings…**. MIDI starts disabled.

## Connect devices

1. In **Devices**, enable MIDI and select input sources/output destinations.
2. Set input channels or **Any**, and output default channels.
3. Choose **Apply Device Settings**. **Refresh Devices** discovers newly connected ports. Automatic reconnect retries saved device identities.
4. Review **Activity** for recent incoming/outgoing messages, connections, and ignored input.

macOS exposes CoreMIDI hardware, IAC, and configured network-session endpoints. Optional **WorshipCue Input / Output** virtual ports let other applications send and receive. Windows uses WinMM devices and installed virtual/network drivers such as loopMIDI or rtpMIDI.

## Map input controls

In **Input Mappings**, add a named trigger for Note On, Note Off, or Control Change. Choose source, channel, number, and exact or **Any** value. **Learn next input message** fills the trigger fields from an enabled source.

Choose an action such as Next/Previous, Blackout, Clear Text, Restore, presentation selection/take, a source-page take, timing/media controls, or note release. **Occurrence** identifies a repeated page within an arrangement. Service targets belong to their specific saved entry.

Overlapping enabled triggers are rejected. Input mappings save in their editor. Cue controls operate in Show mode with modal dialogs and popup menus closed. A mapped Blackout remains available during setup. Use Restore to release it once cue controls resume. Learning and monitoring remain available during modal editing.

## Send MIDI from slides

Right-click a slide and choose **MIDI Actions / Input Trigger…**, or choose **Presentation → MIDI Actions / Input Trigger…**. Add ordered output commands with destination, channel 1–16, Note On/Off or Control Change, number 0–127, and velocity/value. **Save** commits the output commands.

Output commands fire on explicit takes and actual navigation/timed advances. Repainting and audience Blackout/Clear/Restore controls keep the current command sequence captured. Disconnected destinations skip outgoing commands.

Held notes and unchanged CC values fire once. Debounce and outgoing echo suppression reduce retriggering. Use distinct channels or numbers when routing between applications. **Release WorshipCue Notes** attempts Note Off for owned notes.

[Troubleshooting](troubleshooting.md) · [Presentation navigation](navigation.md)
