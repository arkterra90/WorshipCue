# WorshipCue application icon

The WorshipCue icon combines an ivory worship cross with a mint presentation cue
on a navy rounded tile, using the application's existing colors.

- `worshipcue.png`: 1024 x 1024 RGBA source used by Qt's compiled resources.
- `WorshipCue.icns`: macOS icon containing standard and Retina sizes, from 16 to
  1024 pixels. CMake includes it in the app bundle and sets `CFBundleIconFile`.

The PNG is compiled into the app, so its window/Dock icon does not depend on the
working directory. The macOS bundle icon is applied on every rebuild, including
through `Rebuild WorshipCue.command`. Quit and reopen the rebuilt app to see it.

Generated with the built-in imagegen tool, then resized and packaged with macOS
`sips` and `iconutil`. The generated original is retained in the imagegen library.

## Generation prompt

Use case: logo-brand.
Asset type: final macOS application icon for WorshipCue, a church worship slide and media presentation program.
Create one polished, original square app icon, 1024 x 1024. Use the existing app's deep navy and luminous mint-green palette, with a warm white accent. The icon is a beautifully finished navy rounded-square tile with a subtle premium satin gradient and restrained dimensional edge. Inside is one bold, balanced logo mark integrating a worship cross and a forward presentation cue/play shape. Make the faith symbol and forward cue feel like a single coherent, thoughtfully designed geometric emblem, not a collection of clip art. Strong visual hierarchy, broad clean shapes, generous negative space, exceptional legibility at 32 pixels. Contemporary, calm, inviting, professional desktop software aesthetic. Head-on orthographic view, tile centered and occupying approximately 90% of the square canvas, with consistent transparent outer margins. Only the icon, no surrounding scene, no mockup, no lettering, no words, no watermark. Background outside the rounded tile must be genuinely transparent, not a checkerboard image. Deliver a production-ready bitmap with smooth antialiased edges.
