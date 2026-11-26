# Dockbreak

An OBS Studio plugin that keeps popped-out docks open when the OBS main window is minimised.

## The problem

A dock you pop out of OBS still belongs to OBS's main window as far as Windows is concerned. Minimise OBS and every popped-out dock disappears with it, and it has no taskbar entry, minimise button or snapping of its own. That rules out the obvious setup for a control panel such as a browser dock: keep the panel up and OBS out of the way.

## What it does

Every dock you pop out becomes a window of its own. It stays on screen when OBS is minimised, gets its own taskbar entry, minimise and maximise buttons (so Windows 11 Snap Layouts work) and a title bar that matches OBS's theme. Docked docks are left alone.

It applies to every dock, whether it comes from OBS, another plugin, or a custom browser dock. It has no settings.

## Behaviour

| State | What Dockbreak does |
| --- | --- |
| Dock is in the main window | Nothing. |
| Dock is popped out | Makes it independent of the main window, so minimising OBS leaves it open and in front. Gives it a normal title bar with minimise and maximise buttons (so Windows 11 Snap Layouts work), and a taskbar button. You can shape it however you like; it keeps that shape while it stays popped out, including across OBS restarts. |
| Popped-out dock is docked again | Its popped-out shape is forgotten. The next time it is popped out, however that is done, it starts from its default shape: its docked size, where it was docked. |

- A dock that was popped out when OBS closed comes back popped out and independent when OBS starts. OBS restores the layout; Dockbreak releases the dock.
- Dragging a dock out of the main window leaves it under the cursor, at its docked size.
- A dock that was maximised when OBS closed comes back maximised, and its restore button returns it to the size it had before.
- Docks added while OBS is running, such as a new custom browser dock, are handled straight away.
- On Windows, a popped-out dock's title bar is light or dark to match OBS's theme, and follows it when you change theme.
- Repairs OBS's dock layout when a re-docked dock isn't placed. On OBS 33 (Qt 6.11), after a popped-out dock has been moved by its title bar, double-clicking a dock to re-dock it can leave it docked but invisible, and every later re-dock too, until OBS restarts. Dockbreak notices a re-docked dock outside the dock areas and rebuilds the layout the way a restart does. Popped-out docks blink once while it does.
- A dock you minimise stays minimised when OBS is minimised or restored; bring it back from its taskbar entry.
- Double-clicking a popped-out dock's title bar re-docks it, as in OBS; it does not maximise it. Use the maximise button or Snap Layouts for that.
- Lock UI is OBS's setting and is left alone.

## How it works

A floating `QDockWidget` is a top-level window whose Qt parent is still the main window. Qt gives it the main window as its *transient parent*. On Windows that becomes the native **owner** of the window, and Windows hides owned windows when their owner is minimised. Qt itself does not hide it.

Dockbreak clears the transient parent of each floating dock, so the window has no owner. It does not reparent the dock:

- The dock stays in OBS's dock layout, so OBS still saves and restores it as floating, with its geometry and visibility. (OBS saves the layout in its close handler before any plugin event fires. A dock reparented out of the main window would be missing from the saved layout.)
- Re-docking works as usual, by double-clicking the title bar or dragging the dock back.
- No native window is destroyed or recreated, so docks that host a browser (CEF) are not re-hosted.

Without an owner, the dock also needs what owning it used to give it. Dockbreak:

- gives it a taskbar button (`WS_EX_APPWINDOW`);
- swaps Qt's slim tool-window title bar, which Windows draws with a close button only, for a normal one with minimise and maximise buttons, keeping the dock's contents exactly where they were;
- brings it to the front, without taking focus, when it pops out and when OBS is minimised or restored, since Windows no longer keeps it above OBS;
- takes title-bar double-clicks away from Windows, so they re-dock the dock instead of maximising it.

Qt draws a dark title bar only when Windows itself is in dark mode *and* the window's palette is dark, so a dark OBS theme on a light-mode Windows used to get light title bars. Dockbreak sets the dock's title bar from OBS's theme instead (`obs_frontend_is_theme_dark()`, with the same DWM attribute Qt uses). It sets it again when OBS changes theme, and whenever Qt has re-decided it.

The detail is in the comments in [`src/DockBreak.hpp`](src/DockBreak.hpp).

## Stored data

The only thing Dockbreak saves is, for each popped-out dock, its normal size and whether it is maximised, keyed by the dock's object name. It is used only to bring a dock that was maximised when OBS closed back maximised over its normal size. It lives in the plugin's own file, not in OBS's configuration:

```
%APPDATA%\obs-studio\plugin_config\obs-dockbreak\detached-docks.json
```

The file is disposable. If you delete it, the only effect is that a dock maximised when OBS closed comes back as a large ordinary window instead.

## Requirements

- **OBS Studio 32.0.0 or later.** The plugin checks the running OBS version and refuses to load on anything older. Tested on OBS 33.0.1.
- **Windows.** Linux and macOS are not supported yet.

## Installing

Download `obs-dockbreak-<version>-windows-x64.zip` from the [latest release](https://github.com/FourCourtJester/OBS-Dockbreak/releases). For a build of an unreleased branch, use the Artifacts of a [CI run](https://github.com/FourCourtJester/OBS-Dockbreak/actions) instead (you need to be signed in to GitHub; the artifact is a zip with the plugin's zip inside).

**Close OBS before copying anything in.** The plugin is loaded at startup, and a running OBS holds the file open.

The zip contains an `obs-dockbreak` folder already in the right shape. Drop it whole into:

```
C:\ProgramData\obs-studio\plugins\
```

so that you end up with:

```
C:\ProgramData\obs-studio\plugins\obs-dockbreak\bin\64bit\obs-dockbreak.dll
C:\ProgramData\obs-studio\plugins\obs-dockbreak\data\locale\en-US.ini
```

Paste `%ProgramData%\obs-studio\plugins` into the Explorer address bar to get there, and create the `plugins` folder if it isn't there. No admin rights are needed, and OBS updates leave it alone.

**It is not `%APPDATA%`.** Many guides say it is, but `%APPDATA%\obs-studio` holds your settings, not plugins. OBS looks in `C:\ProgramData`.

Two cases where that folder is wrong:

- **Portable mode.** A portable OBS never looks in `C:\ProgramData`. Install next to OBS instead (below), in the portable folder.
- **Installing next to OBS itself.** This works too, but the files are split rather than kept in one folder, and it needs admin rights for `Program Files`:

  ```
  C:\Program Files\obs-studio\obs-plugins\64bit\obs-dockbreak.dll
  C:\Program Files\obs-studio\data\obs-plugins\obs-dockbreak\locale\en-US.ini
  ```

On OBS 33 the plugin manager marks the `C:\ProgramData` folder layout as "Legacy". It still loads normally.

To uninstall, close OBS and delete the `obs-dockbreak` folder (or, if you installed next to OBS, the two files above).

### Checking it loaded

Start OBS and open the log (Help > Log Files > View Current Log), then search for `obs-dockbreak`. A successful load logs:

```
[obs-dockbreak] plugin loaded successfully (version 1.0.0, build <build>)
```

`<build>` matches the end of a CI artifact's name, so you can tell which download is installed. An OBS older than 32.0.0 logs `requires OBS 32.0.0 or later` and the plugin stays off. Dockbreak is also listed in Tools > Plugin Manager.

## Building

The build setup is the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate). Pushes and pull requests are built by GitHub Actions, and the build artifacts can be downloaded from the workflow run.

To build locally on Windows (Visual Studio 17 2022, CMake 3.30):

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The build compiles against the OBS 31.1.1 headers pinned in `buildspec.json`, the template's current pin. Every API the plugin uses is unchanged in 32.x. The 32.0.0 minimum is enforced at runtime, not by the headers.

## Manual test (Windows)

Use a custom browser dock for at least one round, and check that it keeps rendering throughout.

**Staying open**

1. Pop out a dock. Minimise OBS. The dock stays on screen and in front.
2. Restore OBS. The dock is in front of OBS.
3. Minimise OBS to the system tray, if enabled. The dock stays open.
4. Switch Lock UI on and off with a dock popped out, then minimise OBS. The dock stays open.

**Popping out and re-docking**

5. Pop out a dock, by dragging it out and by the float button. It appears in front of OBS. A second or two later, clicking OBS brings OBS in front of it.
6. Double-click a popped-out dock's title bar. It re-docks.
7. Move a popped-out dock by its title bar, then double-click it, and then double-click a second dock. Both re-dock into place (within about a second).
8. Reshape a popped-out dock, re-dock it and pop it out again. It starts at its docked size, where it was docked.

**Title bar and window**

9. A popped-out dock has minimise, maximise and close buttons and a taskbar entry; re-docked, the entry disappears.
10. Minimise the dock itself, then minimise and restore OBS. The dock stays minimised until you bring it back from its taskbar entry.
11. Drag a popped-out dock to the top of the screen. Windows' snap bar appears. Snap it, re-dock it, pop it out again: it starts at its docked size.
12. Maximise a popped-out dock, double-click its title bar: it re-docks, and pops out again at its docked size, not maximised.
13. Restart OBS with a dock maximised. It comes back maximised, and its restore button shrinks it.
14. Restart OBS with a dock popped out. It comes back popped out, at the same size, and stays open when OBS is minimised.

**Theme**

15. With a dark OBS theme, a popped-out dock's title bar is dark, with Windows in light mode and in dark mode.
16. Switch OBS between a light and a dark theme (Settings > Appearance). The dock's title bar follows straight away.

## Known limitations

- **Z-order.** A popped-out dock is no longer owned by OBS, so Windows no longer keeps it above OBS by itself. Dockbreak brings it to the front when it pops out and when OBS is minimised or restored. Otherwise OBS and the dock stack like any two windows; the dock's taskbar entry brings it back.
- **Taskbar grouping.** A popped-out dock belongs to the OBS process, so Windows groups its taskbar entry with OBS's own button (shown as a second window under the OBS icon) unless taskbar buttons are set never to combine.
- **Main window title bar.** Dockbreak changes only the docks. OBS's own main window keeps Qt's rule, so with a dark OBS theme on a light-mode Windows, the main window's title bar is light while popped-out docks are dark.
- **Old Windows 10.** Versions before 1809 have no dark title bars, so docks stay light there.
- **Not Dockbreak's:** on OBS 33, a docked dock closed with its X button can come back on the next start; closing it from the Docks menu doesn't. This happens without Dockbreak too.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
