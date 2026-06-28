#!/usr/bin/env bash
# hyprtasking FORK dev helper — test the fork build without touching daily config.
#
#   ./dev.sh build     just compile the fork
#   ./dev.sh test      build + load the FORK build, adaptive grid ON (this session only)
#   ./dev.sh restore   go back to the stable stock hyprtasking build
#
# The fork-only `grid:adaptive` key is enabled at RUNTIME here (not written into
# ~/.config/hypr/custom/general.lua), so the daily/stock build never sees an
# unknown key and never throws a config-error overlay.
set -uo pipefail

DIR="$HOME/Projects/hyprtasking"
DEV_SO="$DIR/build/libhyprtasking.so"
CACHE="/var/cache/hyprpm/$USER"
STOCK_SO="$CACHE/hyprtasking/hyprtasking.so"
SCROLL_SO="$CACHE/hyprland-scroll-overview/scrolloverview.so"
STATE="$HOME/.config/hypr/custom/overview.state"

build() { meson compile -C "$DIR/build"; }

unload_overviews() {
  hyprctl plugin unload "$STOCK_SO"  >/dev/null 2>&1 || true
  hyprctl plugin unload "$DEV_SO"    >/dev/null 2>&1 || true
  hyprctl plugin unload "$SCROLL_SO" >/dev/null 2>&1 || true
}

case "${1:-test}" in
  build) build ;;

  test|load|reload)
    build || { echo "build failed — not loading"; exit 1; }
    echo hyprtasking > "$STATE"
    unload_overviews; sleep 0.3
    hyprctl plugin load "$DEV_SO" || { echo "load failed"; exit 1; }
    hyprctl reload >/dev/null 2>&1
    # fork-only adaptive, runtime only. `hyprctl keyword` is rejected in Lua mode;
    # hl.config() via dispatch applies it (the dispatch-return error is harmless).
    hyprctl dispatch 'hl.config({ plugin = { hyprtasking = { grid = { adaptive = true } } } })' >/dev/null 2>&1
    notify-send -t 2500 "hyprtasking fork" "Dev build loaded — adaptive ON (test session)" 2>/dev/null || true
    echo "FORK loaded, adaptive ON. Open with 4-finger up. Run './dev.sh restore' when done."
    ;;

  restore)
    echo hyprtasking > "$STATE"
    unload_overviews; sleep 0.3
    hyprctl plugin load "$STOCK_SO" >/dev/null 2>&1 || true
    hyprctl reload >/dev/null 2>&1
    notify-send -t 2500 "hyprtasking" "Restored stable stock build" 2>/dev/null || true
    echo "Restored stock build (stable, adaptive off). SUPER+CTRL+1 for scrolloverview."
    ;;

  *) echo "usage: dev.sh {build|test|restore}"; exit 1 ;;
esac
