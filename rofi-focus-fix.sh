#!/usr/bin/env bash
# rofi-focus-fix.sh <filter> [rofi args...]
#
# Wraps rofi-hyprspace and re-asserts focus after rofi closes.
#
# Problem: follow_mouse=1 refocuses whatever window is under the cursor
# when rofi (a floating window) closes, overwriting hyprspace's selection.
#
# Fix: hyprspace writes the selected address to /tmp/hyprspace-selection
# before exit. We read it after rofi closes, wait for follow_mouse to
# settle, then re-assert focus.
#
# Hyprland v0.57+: hyprctl dispatch focuswindow fails because the lua router
# can't parse "address:0x..." args. Use hyprctl eval instead.
#
# Usage: rofi-focus-fix.sh <filter> [rofi args...]
#   filter: all | ws+mon | ws | mon

set -euo pipefail

FILTER="${1:-ws+mon}"
shift

# Resolve Hyprland env — not inherited from Hyprland exec bind dispatcher
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"
export HYPRLAND_INSTANCE_SIGNATURE
HYPRLAND_INSTANCE_SIGNATURE=$(ls -t /run/user/1000/hypr/ 2>/dev/null | grep -v "\.log" | head -1)
export HYPRSPACE_FILTER="$FILTER"

# Clear any stale selection from a previous run
rm -f /tmp/hyprspace-selection

rofi -show hyprspace "$@"

# Read selection written by hyprspace _result callback before it exits
[[ ! -f /tmp/hyprspace-selection ]] && exit 0
TARGET=$(tr -d '[:space:]' < /tmp/hyprspace-selection)
rm -f /tmp/hyprspace-selection
[[ -z "$TARGET" ]] && exit 0

# Wait for follow_mouse refocus to settle, then re-assert atomically.
# v0.57: use hyprctl eval with hl.dsp.focus({window=...}) since hyprctl dispatch
# can no longer parse "focuswindow address:0x..." via the lua evaluator.
sleep 0.2
hyprctl eval "
local w = hl.get_window('address:${TARGET}')
if w then
    hl.dispatch(hl.dsp.focus({window=w}))
end
" >/dev/null 2>&1
