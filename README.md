# Dockbreak

An OBS Studio plugin that keeps popped-out docks open when the OBS main window is minimised.

When a dock is popped out (floating) in OBS, Windows still treats it as belonging to the main window and hides it whenever the main window is minimised. With Dockbreak, any dock you pop out stays on screen until you close it or dock it again.

The plugin applies to every dock, whether it comes from OBS, another plugin, or a custom browser dock. It has no settings.

## Behaviour

| State | What Dockbreak does |
| --- | --- |
| Dock is in the main window | Nothing. |
| Dock is popped out | Makes it independent of the main window, so minimising OBS leaves it open, and gives it a taskbar button. Remembers where it is. |
| Popped-out dock is docked again | Nothing. Its last position is kept, so if it is popped out again (float button, double-click on the title bar, or the Docks menu) it reappears in the same place. |

- A dock that was popped out when OBS closed comes back popped out and independent when OBS starts. OBS restores the layout; Dockbreak releases the dock.
- Dragging a dock out of the main window leaves it under the cursor. The saved position is not applied, so the dock is not snatched away mid-drag.
- If a saved position is no longer on any screen (for example, a monitor was unplugged), the dock is left where Qt puts it.
- Docks added while OBS is running, such as a new custom browser dock, are handled straight away.
- On Windows, a popped-out dock's title bar is light or dark to match OBS's theme, and follows it when you change theme.
- Repairs OBS's dock layout when a re-docked dock isn't placed. On OBS 33 (Qt 6.11), after a popped-out dock has been moved by its title bar, double-clicking a dock to re-dock it can leave it docked but invisible, and every later re-dock too, until OBS restarts. Dockbreak notices a re-docked dock outside the dock areas and rebuilds the layout the way a restart does. Popped-out docks blink once while it does.
- Lock UI is OBS's setting and is left alone.

## How it works

A floating `QDockWidget` is a top-level window whose Qt parent is still the main window. Qt gives it the main window as its *transient parent*. On Windows that becomes the native **owner** of the window, and Windows hides owned windows when their owner is minimised. Qt itself does not hide it.

Dockbreak clears the transient parent of each floating dock, so the window has no owner. It also sets `WS_EX_APPWINDOW` on the window, which gives the dock a taskbar button while keeping it a Qt tool window, so its frame and geometry don't change. It does not reparent the dock:

- The dock stays in OBS's dock layout, so OBS still saves and restores it as floating, with its geometry and visibility. (OBS saves the layout in its close handler before any plugin event fires. A dock reparented out of the main window would be missing from the saved layout.)
- Re-docking works as usual, by double-clicking the title bar or dragging the dock back.
- No native window is destroyed or recreated, so docks that host a browser (CEF) are not re-hosted.

Qt draws a dark title bar only when Windows itself is in dark mode *and* the window's palette is dark, so a dark OBS theme on a light-mode Windows used to get light title bars. Dockbreak sets the dock's title bar from OBS's theme instead (`obs_frontend_is_theme_dark()`, with the same DWM attribute Qt uses). It sets it again when OBS changes theme, and whenever Qt has re-decided it.

The detail is in the comments in [`src/DockBreak.hpp`](src/DockBreak.hpp).

## Stored data

The only thing Dockbreak saves is the last floating position of each dock, keyed by the dock's object name. It lives in the plugin's own file, not in OBS's configuration:

```
%APPDATA%\obs-studio\plugin_config\obs-dockbreak\detached-docks.json
```

The file is disposable. If you delete it, the only effect is that docks reopen where Qt puts them.

## Requirements

- **OBS Studio 32.0.0 or later.** The plugin checks the running OBS version and refuses to load on anything older.
- **Windows.** Linux and macOS are not supported yet.

## Building

The build setup is the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate). Pushes and pull requests are built by GitHub Actions, and the build artifacts can be downloaded from the workflow run.

To build locally on Windows (Visual Studio 17 2022, CMake 3.30):

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The build compiles against the OBS 31.1.1 headers pinned in `buildspec.json`, the template's current pin. Every API the plugin uses is unchanged in 32.x. The 32.0.0 minimum is enforced at runtime, not by the headers.

## Manual test (Windows)

1. Pop out a dock. Minimise OBS. The dock stays on screen.
2. Restore OBS and dock the dock again. It goes back into the main window.
3. Pop it out again with the float button. It reappears where it was.
4. Restart OBS with the dock popped out. It comes back popped out, and stays open when OBS is minimised.
5. Do the same with a custom browser dock, and check that it still renders after each step.
6. Switch Lock UI on and off with a dock popped out, then minimise OBS. The dock stays open.
7. Minimise OBS to the system tray, if enabled. The dock stays open.
8. Click the OBS main window while a dock is popped out over it, so the dock goes behind OBS. The dock has its own taskbar entry, and clicking it brings the dock back to the front.
9. Re-dock the dock. Its taskbar entry disappears.
10. Check Alt+Tab with a dock popped out, and note whether the dock is listed.
11. With a dark OBS theme, pop out a dock. Its title bar is dark, with Windows in light mode and in dark mode.
12. With a dock popped out, switch OBS between a light and a dark theme (Settings > Appearance). The dock's title bar follows straight away, without being clicked.
13. With a dock popped out, switch Windows between light and dark mode. The dock's title bar keeps OBS's theme.

## Known limitations

- **Z-order.** A floating dock used to stay above OBS's main window because OBS owned it. Without an owner, clicking the main window can bring it in front of the dock. The dock's taskbar entry is the way back to it.
- **Taskbar grouping.** A popped-out dock belongs to the OBS process, so Windows groups its taskbar entry with OBS's own button (shown as a second window under the OBS icon) unless taskbar buttons are set never to combine.
- **Main window title bar.** Dockbreak changes only the docks. OBS's own main window keeps Qt's rule, so with a dark OBS theme on a light-mode Windows, the main window's title bar is light while popped-out docks are dark.
- **Old Windows 10.** Versions before 1809 have no dark title bars, so docks stay light there.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
