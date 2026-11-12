# Included background collection

Twenty original 16:9 still backgrounds, each 3840 × 2160 pixels. These JPEGs
are embedded in WorshipCue's executable with Qt resources and work offline
in new and existing libraries. The catalogue and stable assignment paths live
in `src/core/BuiltInBackgrounds.h`.

The artwork uses original gradients, curves, contours, and geometric shapes,
with quiet central areas for lyrics. It contains no stock imagery, fonts,
logos, or external textures. The collection is covered by the repository's
MIT license. [View the collection](../../docs/backgrounds-preview.jpg).

The optional Qt 6 artwork tool reproduces all finished assets and the contact
sheet. It is independent of the application build; end users need only the
bundled app. From the repository root:

```sh
cmake -S tools/generate-backgrounds -B /tmp/worshipcue-artwork \
  -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build /tmp/worshipcue-artwork
QT_QPA_PLATFORM=offscreen /tmp/worshipcue-artwork/generate-backgrounds \
  assets/backgrounds docs/backgrounds-preview.jpg
```

Use your installed Qt prefix (for example `/opt/homebrew/opt/qt` on macOS).
The application renderer currently downsamples backgrounds to its 1920 × 1080
canvas and adds the same dimming layer used for imported images.
