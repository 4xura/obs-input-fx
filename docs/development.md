# Development and validation

## Build

Use an x64 Visual Studio C++ toolchain with the Windows SDK. `build.cmd` locates it through the Visual Studio Installer's `vswhere` utility. Run from any directory:

```bat
build.cmd --tests
```

The plugin is `build\obs-input-fx.dll`. The optional visual fixture is `build\input-fx-fixture.exe`. The fixture opens a simple opaque window with a center crosshair and contains no user data.

Keep source formatting consistent with `.clang-format`.

## Check the complete rendering path

1. Install the plugin into a test OBS installation, then restart OBS.
2. Start the fixture and add it as a Window Capture in an isolated scene collection.
3. Scale the capture to cover the whole canvas. An empty or unavailable capture is insufficient: it cannot catch effects being drawn behind an opaque source.
4. Press a shortcut and confirm keycaps appear above the application in normal preview.
5. Enable a persistent cursor ring and place the pointer on the fixture's crosshair.
6. Move the application window. The ring should remain aligned with its captured crosshair.
7. Crop the capture, then change its source scale and position. The ring should remain on the same captured point.
8. Record a short clip and play it back. Check keycaps and the ring in the encoded frames.
9. Exit OBS normally and check its log for clean plugin shutdown and memory leaks.

## Check settings

- Both color swatches should open the Windows palette and color picker.
- Higher **Transparency** values should make the corresponding effect more transparent.
- **Test effects** should work after changing a color or other appearance setting.
- Keyboard filtering should hide ordinary letters and numbers by default while showing modifier shortcuts and navigation keys.
- A visibility hotkey should work after restarting OBS.
- A selected capture target should be retained after restart.

## Validation completed for 1.1.0

The effects were checked over an actual opaque captured app, including an F24 test input through Windows `SendInput`, moved-window alignment, crop offsets, and zoom/pan transforms. A short MP4 was decoded and checked for both effects. Invalid transport tokens were rejected. OBS exited normally with zero reported memory leaks.

OBS Studio 32.2.2 on Windows x64 is the validated environment. Other platforms and OBS major versions are not currently covered.
