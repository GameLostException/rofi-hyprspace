# rofi-hyprspace

A rofi plugin that shows windows on the **current Hyprland workspace and monitor only**, with app icons and `Class — Title` display format.

## Why

Rofi's built-in `window` mode shows all windows across all workspaces with no filtering option. This plugin queries `hyprctl clients` and filters by active workspace + active monitor, giving you a focused Alt+Tab experience.

## Features

- Shows only windows on the current workspace and monitor
- App icons via rofi's icon fetcher (same as built-in window mode)
- Displays `AppClass — Window Title`
- Floating windows excluded (they're overlays, not switcher targets)
- Token-based search (type to filter)

## Requirements

- rofi ≥ 2.0.0
- Hyprland (uses `hyprctl`)
- gcc
- pkg-config with `rofi`, `glib-2.0`, `cairo`

## Build & Install

```bash
make
make install
```

Installs to `~/.local/lib/rofi/rofi-hyprspace.so`.

## Filter Modes

Set via `HYPRSPACE_FILTER` environment variable:

| Value | Description |
|-------|-------------|
| `ws+mon` | Current workspace **and** current monitor (default) |
| `ws` | Current workspace, all monitors |
| `mon` | Current monitor, all workspaces |
| `all` | All windows across all workspaces and monitors |

## Usage

```bash
# Current workspace + monitor (Alt+Tab)
HYPRSPACE_FILTER=ws+mon rofi -show hyprspace -plugin-path ~/.local/lib/rofi

# All windows (Super+Tab)
HYPRSPACE_FILTER=all rofi -show hyprspace -plugin-path ~/.local/lib/rofi
```

### Hyprland keybinds

```ini
# ~/.config/hypr/hyprland.conf
bindl = ALT, Tab, exec, rofi -show hyprspace \
    -plugin-path ~/.local/lib/rofi \
    -theme ~/.config/rofi/hyprland.rasi \
    -window-format "{c} — {t}"
```

## How it works

On init, the plugin:
1. Calls `hyprctl activeworkspace -j` to get current workspace ID
2. Calls `hyprctl activewindow -j` to get current monitor ID  
3. Calls `hyprctl clients -j` and filters windows matching both
4. Builds display entries as `ShortClass — Title`
5. Fetches app icons via rofi's icon fetcher using the window class

On selection, dispatches `hyprctl dispatch focuswindow address:<addr>`.

## Limitations

- Simple JSON parser (no dependency on jansson/json-c) — assumes well-formed hyprctl output
- Floating windows are excluded by design
- Requires hyprctl in PATH

## TODO

- [ ] Include floating windows as optional config
- [ ] Show workspace badge on icon for cross-workspace preview mode
- [ ] Config option for title truncation length
