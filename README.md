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

> **Important**: Use `rofi-focus-fix.sh` instead of calling rofi directly.
> Hyprland's `follow_mouse=1` overwrites focus when rofi (a floating window)
> closes. The wrapper re-asserts the selection after follow_mouse settles.

```bash
# ~/Tools/rofi-focus-fix.sh ws+mon [rofi args...]  ← ALT+TAB
# ~/Tools/rofi-focus-fix.sh all    [rofi args...]  ← SUP+TAB
```

### Hyprland keybinds

```lua
-- ~/.config/hypr/hyprland.lua
local mod = "SUPER"
hl.bind(mod .. " + Tab", hl.dsp.exec_cmd("/path/to/rofi-focus-fix.sh all    -plugin-path ~/.local/lib/rofi -theme ~/.config/rofi/hyprland.rasi -hover-select -me-select-entry \"\" -me-accept-entry \"MousePrimary\""))
hl.bind("ALT + Tab",     hl.dsp.exec_cmd("/path/to/rofi-focus-fix.sh ws+mon -plugin-path ~/.local/lib/rofi -theme ~/.config/rofi/hyprland.rasi -hover-select -me-select-entry \"\" -me-accept-entry \"MousePrimary\""))
```

### Why the wrapper?

On selection, hyprspace writes the selected window address to `/tmp/hyprspace-selection`.
`follow_mouse=1` immediately refocuses whatever window is under the cursor when the rofi
floating surface closes (~50ms later), overwriting the selection. The wrapper re-asserts
focus 200ms after rofi exits via `hyprctl eval` with `hl.dsp.focus({window=...})`.

> **Hyprland v0.57+**: `hyprctl dispatch focuswindow address:X` fails because the lua
> router cannot parse `address:0x...` syntax. The wrapper and the plugin both use
> `hyprctl eval "local w = hl.get_window('address:ADDR'); if w then hl.dispatch(hl.dsp.focus({window=w})) end"` instead.

## How it works

On init, the plugin:
1. Calls `hyprctl activeworkspace -j` to get current workspace ID
2. Calls `hyprctl activewindow -j` to get current monitor ID  
3. Calls `hyprctl clients -j` and filters windows matching both
4. Builds display entries as `ShortClass — Title`
5. Fetches app icons via rofi's icon fetcher using the window class

On selection, uses `hyprctl eval` with `hl.get_window()` and `hl.dsp.focus()` to focus
the selected window (Hyprland v0.57+ compatible — `hyprctl dispatch focuswindow` cannot
parse `address:0x...` through the Lua router).

## Limitations

- Simple JSON parser (no dependency on jansson/json-c) — assumes well-formed hyprctl output
- Floating windows are excluded by design
- Requires hyprctl in PATH

## TODO

- [ ] Include floating windows as optional config
- [ ] Show workspace badge on icon for cross-workspace preview mode
- [ ] Config option for title truncation length
