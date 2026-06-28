#!/usr/bin/env bash
# hyprtasking fork dev helper.
#   ./dev.sh build    - compile
#   ./dev.sh load     - build + hot-load our .so (replaces the hyprpm copy live)
#   ./dev.sh restore  - unload ours, reload the hyprpm-installed version
#   ./dev.sh reload   - alias for load
set -uo pipefail
DIR="$HOME/Projects/hyprtasking"
DEV_SO="$DIR/build/libhyprtasking.so"
HYPRPM_SO="/var/cache/hyprpm/$USER/hyprtasking/hyprtasking.so"

build() { meson compile -C "$DIR/build"; }

unload_all() {
  hyprctl plugin unload "$HYPRPM_SO" >/dev/null 2>&1 || true
  hyprctl plugin unload "$DEV_SO"    >/dev/null 2>&1 || true
}

case "${1:-load}" in
  build) build ;;
  load|reload)
    build || { echo "build failed"; exit 1; }
    unload_all; sleep 0.3
    hyprctl plugin load "$DEV_SO"
    hyprctl reload >/dev/null 2>&1
    echo "loaded dev build"
    ;;
  restore)
    unload_all; sleep 0.3
    hyprctl plugin load "$HYPRPM_SO"
    hyprctl reload >/dev/null 2>&1
    echo "restored hyprpm build"
    ;;
  *) echo "usage: dev.sh {build|load|restore}"; exit 1 ;;
esac
