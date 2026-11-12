# Media playback fixture

`media-colors.mov` is an original three-second, 160×90 H.264 movie created for WorshipCue's tests. It has no soundtrack and changes from red to green halfway through. It contains no third-party media.

The fixture exercises real decoding, pause/restart, end-of-video frame retention, and looping through Qt Multimedia. It is not evidence that every user codec is supported on every operating system.

To regenerate on macOS:

```sh
swift generate_media_fixture.swift media-colors.mov
```

The Darwin Qt Multimedia backend needs the Cocoa event dispatcher to deliver AVFoundation callbacks. Run playback tests with `QT_QPA_PLATFORM=cocoa` in a logged-in macOS desktop session; the offscreen backend does not exercise native video decoding. Still images and renderer tests can run offscreen.

Planning Center fixtures `planning-center-plan.json` and `planning-center-song-items.json` are synthetic JSON:API responses based on the published Services schema. They contain original sample text and exercise pagination, pre/during/post ordering, included relationships, labelled lyrics, sequence mapping, internal notes, and unresolved media. They contain no account credentials or real church data.
