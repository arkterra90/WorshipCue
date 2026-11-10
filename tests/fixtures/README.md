# Media playback fixture

`media-colors.mov` is an original three-second, 160×90 H.264 movie created for WorshipCue's tests. It has no soundtrack and changes from red to green halfway through. It contains no third-party media.

The fixture exercises real decoding, pause/restart, end-of-video frame retention, and looping through Qt Multimedia. It is not evidence that every user codec is supported on every operating system.

To regenerate on macOS:

```sh
swift generate_media_fixture.swift media-colors.mov
```

The Darwin Qt Multimedia backend needs the Cocoa event dispatcher to deliver AVFoundation callbacks. Run playback tests with `QT_QPA_PLATFORM=cocoa` in a logged-in macOS desktop session; the offscreen backend does not exercise native video decoding. Still images and renderer tests can run offscreen.
