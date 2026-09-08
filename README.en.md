<p align="center">
  <img src="resources/branding/hlaunch-logo.svg" alt="HLaunch logo" width="160" height="160">
</p>

<h1 align="center">HLaunch</h1>

<p align="center">Your everyday apps, files, and websites, always within reach.</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-x64%20%7C%20x86%20%7C%20ARM64-0078D4?style=flat-square" alt="Windows x64, x86, ARM64">
  <img src="https://img.shields.io/badge/Portable-Single_EXE-16A34A?style=flat-square" alt="Portable single executable">
  <img src="https://img.shields.io/badge/Offline-Local_only-7C3AED?style=flat-square" alt="Offline and local">
</p>

<p align="center"><a href="README.md">简体中文</a> · <strong>English</strong></p>

<p align="center"><a href="https://github.com/Hunlongyu/hlaunch/releases">Releases</a> · <a href="CHANGELOG.md">Changelog</a></p>

HLaunch is a native Windows grid launcher. Organize your everyday shortcuts with icons and tabs, bring them up with a hotkey, and type to search. It runs as a single `HLaunch.exe`, with no installation or additional runtime required.

## 🖼️ Screenshots

<p align="center">
  <img src="docs/images/launcher-custom.png" alt="HLaunch showing the custom tools tab" width="318">
  <img src="docs/images/launcher-development.png" alt="HLaunch showing the development tools tab" width="318">
</p>

## ✨ Highlights

- **Everything in one place**: launch apps, files, folders, shortcuts, and websites from one panel.
- **Easy organization**: drag items in to add them, rearrange icons, move items between tabs, and reorder tabs. Resize the window to add or remove grid rows and columns.
- **Ready when you need it**: press `Alt+Space` to show or hide the panel, or enable activation by hovering at a screen edge or corner.
- **Quick search**: start typing to search across all tabs, then use the arrow keys and Enter to launch a result.
- **Adjustable appearance**: a built-in dark interface with background effects, adjustable opacity, and an always-on-top option. Supports multiple monitors and display scaling.
- **Local and portable**: settings stay on your computer, with no account or telemetry. Optional launch at sign-in is available from the tray menu.

## 🚀 Getting started

1. Place `HLaunch.exe` in a folder of your choice and run it.
2. Press `Alt+Space` to open the panel, then drag apps, files, or shortcuts into the grid.
3. Click an icon to launch it. Right-click to manage items and tabs.
4. Right-click the tray icon and open Settings (设置) to change the hotkey, enable edge activation, or adjust the appearance.

Edge activation is off by default. When enabled, it is suppressed for fullscreen apps by default. The panel's close button hides the window; use the tray menu to exit HLaunch.

## ⌨️ Shortcuts

Except for the global activation hotkey, these shortcuts apply while the panel is focused.

| Action | Keyboard / mouse |
| --- | --- |
| Show / hide the panel | `Alt+Space` (customizable) |
| Search all tabs | Start typing, or `Ctrl+F` |
| Select / launch an item | Arrow keys / `Enter` |
| Switch tabs | Mouse wheel, or `Tab` / `Shift+Tab` |
| Previous / next page | `PageUp` / `PageDown` |
| Add / edit a selected item | `Insert` / `F2` |
| Toggle always-on-top | `Ctrl+Space` |
| Hide the panel | `Esc` (clears the query first when searching) |

## 📁 Data and requirements

Settings and items are stored in the `data` folder beside the executable. If that location cannot be read or written, HLaunch uses `%LOCALAPPDATA%\HLaunch` instead. Copy the active data folder to back it up. Apps inside the portable folder support relative paths, so they can move with the folder.

Primarily targets **Windows 11**, with **x64, x86, and ARM64** build targets. Windows 10 compatibility is provided on a best-effort basis.

<details>
<summary>🛠️ Build from source</summary>

Install MSVC with C++23 support and the desired architecture tools, the Windows SDK, CMake 3.28+, Ninja, and PowerShell 7. Run:

```powershell
pwsh -File scripts/build-release.ps1 -Architecture x64
```

You can also select `x86` or `arm64`. The first configuration downloads build dependencies. Verified executables are placed in `out/release/`.

See [developer documentation](docs/ai/README.md) for more details (in Chinese).

</details>
