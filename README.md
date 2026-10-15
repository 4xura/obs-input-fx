# InputFX for OBS

Clean keyboard, mouse, and cursor effects for OBS Studio. A native Windows plugin with transparent cyberpunk keycaps, animated click rings, and automatic rendering over every scene.

![InputFX keyboard effects](docs/assets/keycaps.png)

**Windows x64 · tested with OBS Studio 32.2.2**

## Install

1. Download the Windows ZIP from [Releases](https://github.com/4xura/obs-input-fx/releases/latest).
2. Close OBS.
3. Extract the ZIP into your OBS installation. The DLL belongs at `obs-plugins\64bit\obs-input-fx.dll` inside that installation.
4. Restart OBS and open **Tools → InputFX… → Test effects**.

No scene sources need to be added. InputFX draws over the composed program output, so existing scenes, new scenes, and new scene collections work automatically.

If you used the earlier **Cyber Input** build, remove `cyber-input.dll` before installing InputFX. The first launch copies its existing settings into InputFX's configuration directory.

## Customize the effects

Open **Tools → InputFX…**.

| Control | What it changes |
| --- | --- |
| Show overlay | Enable or disable all effects |
| Transparency | Make key panels more see-through while keeping text readable |
| Accent color | Choose a color using the palette and picker |
| Size and display time | Adjust keycap size and how long actions remain visible |
| Alignment and margins | Position the keycaps along the bottom |
| Mouse buttons and scrolling | Show mouse-action badges |
| Ordinary typing | Show unmodified letters and numbers; disabled by default |
| Cursor ring | Customize its color, size, thickness, duration, and transparency |
| Always highlight the cursor | Keep a ring visible between clicks |
| Capture target | Follow a visible Window Capture or Display Capture automatically, or select one |

The cursor ring follows the captured window or monitor through source movement, cropping, scaling, and zoom/pan transforms. It stays aligned when the application window moves.

Assign an optional global toggle under **Settings → Hotkeys → InputFX: show/hide overlay**.

## Where the effects appear

The normal OBS preview, Program pane, recordings, and streams include the effects. In Studio Mode, the uncommitted left Preview pane renders its scene separately and does not include the global overlay.

The effects are added to the final program canvas. Isolated source screenshots and source-only recording plugins do not include them. Keyboard and mouse badges work above other capture types; cursor-position mapping currently supports **Window Capture and Display Capture**, not Game Capture.

## Build from source

Install Visual Studio 2022 or Build Tools with **Desktop development with C++** and the Windows SDK, then run:

```bat
build.cmd
```

The build discovers Visual Studio with `vswhere` and creates `build\obs-input-fx.dll`. It links the Visual C++ runtime statically. No OBS SDK download or Qt development installation is required; the plugin resolves the OBS 32 C APIs it uses from the running application.

To build the small Windows capture fixture as well:

```bat
build.cmd --tests
```

See [development and validation](docs/development.md) for the test procedure and [architecture](docs/architecture.md) for the implementation.

## Configuration and input handling

Settings live in OBS's `plugin_config/obs-input-fx/settings.json` directory. Portable OBS installations use their own configuration location. The embedded renderer is extracted beside the settings when the plugin loads.

Input is processed in memory. Keystrokes are not written to a log. The renderer connection stays on `127.0.0.1` and uses a random per-session token. InputFX needs no Input Overlay, Keyviz, Lua script, or external service.

## Troubleshooting

- **Nothing appears:** check **Show overlay**, then click **Test effects**. Ordinary typing is hidden by default; try a shortcut such as Ctrl+C.
- **No cursor ring:** select the intended Window Capture or Display Capture in **Capture target**. The pointer must be inside the visible captured area. Use **Always highlight the cursor** for a persistent ring.
- **Two overlays appear:** remove the earlier `cyber-input.dll` or manually added Browser Source.
- **Plugin does not load:** confirm you installed the x64 DLL into the OBS installation you are launching. Other OBS major versions have not been validated.

To uninstall, close OBS and remove `obs-input-fx.dll`. You may keep the settings for a later reinstall.

## License

[MIT](LICENSE).
