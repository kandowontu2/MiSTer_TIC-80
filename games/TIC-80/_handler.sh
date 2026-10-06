#!/bin/sh
# Called by the existing MiSTer Frontier Master Daemon on core entry.
set -eu
game_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
sd_root=$(CDPATH= cd "$game_dir/../.." && pwd)
log_dir=$sd_root/logs/TIC-80
save_dir=$sd_root/saves/TIC-80
frontend=studio
if [ -f "$game_dir/frontend.txt" ]; then
    frontend=$(cat "$game_dir/frontend.txt")
fi
case "$frontend" in
    studio) program=$game_dir/TIC-80-Studio ;;
    player) program=$game_dir/TIC-80 ;;
    *) printf 'TIC-80: frontend.txt must contain studio or player\n' >&2; exit 2 ;;
esac
if [ ! -x "$program" ]; then
    printf 'TIC-80: missing executable %s\n' "$program" >&2
    exit 1
fi
mkdir -p "$log_dir" "$save_dir" "$game_dir/Carts"
cd "$game_dir"
sleep 1
if [ -f "$log_dir/tic80.log" ]; then
    mv -f "$log_dir/tic80.log" "$log_dir/tic80.prev.log"
fi
case "$frontend" in
    studio) exec "$program" --folder "$game_dir/Carts" --saves "$save_dir" > "$log_dir/tic80.log" 2>&1 ;;
    player) exec "$program" --serve "$save_dir" > "$log_dir/tic80.log" 2>&1 ;;
esac
