#!/bin/bash
# TIC-80 installer: Frontier launches games/TIC-80/_handler.sh, as for PICO-8.
[ -n "${BASH_VERSION:-}" ] || exec bash "$0" "$@"
set -Eeuo pipefail
umask 022

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=/media/fat
BUNDLE=$HERE/TIC80-install
ACTION=install
RESTORE=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --root|--bundle|--rollback)
            [ "$#" -ge 2 ] || { echo "Missing argument for $1" >&2; exit 2; }
            case "$1" in
                --root) ROOT=$2 ;;
                --bundle) BUNDLE=$2 ;;
                --rollback) ACTION=rollback; RESTORE=$2 ;;
            esac
            shift 2 ;;
        --check) ACTION=check; shift ;;
        --help)
            echo 'Run Install_TIC80.sh from the MiSTer Scripts menu.'
            echo 'Options: --check, --rollback BACKUP, --root SD_ROOT, --bundle DIRECTORY'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
die() { echo "TIC-80: $*" >&2; exit 1; }
ROOT=$(CDPATH= cd -- "$ROOT" && pwd)
[ -f "$ROOT/MiSTer" ] || die "MiSTer executable missing from $ROOT"
hash() { sha256sum "$1" | awk '{print $1}'; }
MAIN_HASH=$(hash "$ROOT/MiSTer")
MASTER=MiSTer_Frontier/Master_Daemon.sh
STARTUP=linux/user-startup.sh
core_path() {
    case "$1" in
        _Other/TIC80_[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9].rbf|games/TIC-80/TIC-80|games/TIC-80/TIC-80-Studio|games/TIC-80/_handler.sh|games/TIC-80/cacert.pem) return 0 ;;
        *) return 1 ;;
    esac
}
safe_target() {
    local path=$1 parent=$ROOT part
    [ ! -L "$ROOT/$path" ] || die "Symlink destination: $path"
    [ ! -e "$ROOT/$path" ] || [ -f "$ROOT/$path" ] || die "Not a regular file: $path"
    # Avoid an unexpected parent symlink redirecting an installer write.
    IFS=/ read -r -a parts <<< "${path%/*}"
    for part in "${parts[@]}"; do
        parent=$parent/$part
        [ ! -L "$parent" ] || die "Symlink parent: $parent"
    done
}
unchanged_main() { [ "$(hash "$ROOT/MiSTer")" = "$MAIN_HASH" ] || die 'MiSTer changed during installation'; }

if [ "$ACTION" = rollback ]; then
    RESTORE=$(CDPATH= cd -- "$RESTORE" && pwd)
    [ -f "$RESTORE/complete" ] && [ -f "$RESTORE/changes.tsv" ] || die 'Not a completed TIC-80 installation backup'
    # Validate the whole rollback before restoring anything. Frontier is shared
    # with other cores, so keep its setup when rolling back just TIC-80.
    while IFS=$'\t' read -r path prior expected; do
        core_path "$path" || continue
        safe_target "$path"
        [ -f "$ROOT/$path" ] && [ "$(hash "$ROOT/$path")" = "$expected" ] || die "Changed since install: $path; rollback aborted"
        if [ "$prior" != absent ]; then
            [ "$prior" = present ] && [ -f "$RESTORE/before/$path" ] || die "Missing backup: $path"
            [ "$(hash "$RESTORE/before/$path")" = "$(cat "$RESTORE/before/$path.sha256")" ] || die "Damaged backup: $path"
        fi
    done < "$RESTORE/changes.tsv"
else
    BUNDLE=$(CDPATH= cd -- "$BUNDLE" && pwd)
    [ -f "$BUNDLE/files.sha256" ] && [ -f "$BUNDLE/frontier.sha256" ] || die 'Incomplete installer bundle'
    declare -A SEEN=()
    PATHS=()
    while read -r expected path extra; do
        [[ "$expected" =~ ^[0-9a-f]{64}$ ]] && [ -z "${extra:-}" ] || die 'Invalid checksum manifest'
        core_path "$path" || die "Forbidden payload path: $path"
        [ -z "${SEEN[$path]:-}" ] || die "Duplicate payload: $path"
        SEEN[$path]=$expected; PATHS+=("$path")
        [ -f "$BUNDLE/payload/$path" ] && [ ! -L "$BUNDLE/payload/$path" ] || die "Missing payload: $path"
        [ "$(hash "$BUNDLE/payload/$path")" = "$expected" ] || die "Checksum failed: $path"
        safe_target "$path"
    done < "$BUNDLE/files.sha256"
    [ "${#PATHS[@]}" = 5 ] || die 'Expected exactly five TIC-80 payloads'
    for path in games/TIC-80/TIC-80 games/TIC-80/TIC-80-Studio games/TIC-80/_handler.sh games/TIC-80/cacert.pem; do
        [ -n "${SEEN[$path]:-}" ] || die "Missing required payload: $path"
    done
    read -r expected path extra < "$BUNDLE/frontier.sha256"
    [[ "$expected" =~ ^[0-9a-f]{64}$ ]] && [ "$path" = Master_Daemon.sh ] && [ -z "${extra:-}" ] || die 'Invalid Frontier manifest'
    [ "$(hash "$BUNDLE/frontier/Master_Daemon.sh")" = "$expected" ] || die 'Frontier checksum failed'
    bash -n "$BUNDLE/frontier/Master_Daemon.sh"
    bash -n "$BUNDLE/payload/games/TIC-80/_handler.sh"
    safe_target "$MASTER"; safe_target "$STARTUP"
    if [ -f "$ROOT/$MASTER" ]; then
        bash -n "$ROOT/$MASTER"
    fi
    if [ "$ACTION" = check ]; then
        unchanged_main
        echo 'TIC-80: bundle verified. MiSTer will be preserved; existing Frontier will be reused.'
        exit 0
    fi
fi

# Replacing a live ARM executable is unsafe on Linux (ETXTBSY), and a live
# frontend must not span two versions of this bundle. Menu entry stops it.
if [ "$ROOT" = /media/fat ] && [ "$(cat /tmp/CORENAME 2>/dev/null || true)" = TIC-80 ]; then
    die 'Load the MiSTer MENU core, then run this script again'
fi
mkdir -p "$ROOT/Scripts"
LOCK=$ROOT/Scripts/.tic80-install.lock
mkdir "$LOCK" 2>/dev/null || die "Another installation is running (lock: $LOCK)"
STAGE=
BACKUP=
COMMITTED=0
finish() {
    local rc=$? path prior expected
    trap - EXIT INT TERM
    if [ "$COMMITTED" = 0 ] && [ -n "$BACKUP" ] && [ -f "$BACKUP/changes.tsv" ]; then
        # Every replacement is journaled before its atomic rename.
        while IFS=$'\t' read -r path prior expected; do
            if [ "$prior" = present ]; then
                cp -p "$BACKUP/before/$path" "$ROOT/$path.tic80-restore.$$" && mv -f "$ROOT/$path.tic80-restore.$$" "$ROOT/$path" || rc=1
            else
                rm -f "$ROOT/$path" || rc=1
            fi
        done < "$BACKUP/changes.tsv"
        echo "TIC-80: installation failed; restored files from $BACKUP" >&2
    fi
    for path in "${PATHS[@]:-}" "$MASTER" "$STARTUP"; do
        [ -z "$path" ] || rm -f "$ROOT/$path.tic80-new.$$" "$ROOT/$path.tic80-restore.$$"
    done
    [ -z "$STAGE" ] || rm -rf -- "$STAGE"
    rmdir "$LOCK" || true
    exit "$rc"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ "$ACTION" = rollback ]; then
    while IFS=$'\t' read -r path prior expected; do
        core_path "$path" || continue
        if [ "$prior" = present ]; then
            cp -p "$RESTORE/before/$path" "$ROOT/$path.tic80-restore.$$"
            mv -f "$ROOT/$path.tic80-restore.$$" "$ROOT/$path"
        else
            rm -f "$ROOT/$path"
        fi
    done < "$RESTORE/changes.tsv"
    unchanged_main
    COMMITTED=1
    echo 'TIC-80: core files restored. Frontier, carts, saves and settings retained.'
    exit 0
fi

STAGE=$(mktemp -d "$ROOT/Scripts/.tic80-stage.XXXXXX")
BACKUP=$ROOT/Scripts/TIC80-backups/$(date -u +%Y%m%dT%H%M%SZ).$$
mkdir -p "$BACKUP/before"
: > "$BACKUP/changes.tsv"
printf '%s\n' "$MAIN_HASH" > "$BACKUP/preserved-MiSTer.sha256"
promote() {
    local path=$1 source=$2 mode=$3 prior=absent expected
    expected=$(hash "$source")
    [ -z "${SEEN[$path]:-}" ] || [ "$expected" = "${SEEN[$path]}" ] || die "Payload changed after verification: $path"
    if [ -f "$ROOT/$path" ] && [ "$(hash "$ROOT/$path")" = "$expected" ]; then
        chmod "$mode" "$ROOT/$path"
        return
    fi
    mkdir -p "$ROOT/${path%/*}" "$BACKUP/before/${path%/*}"
    if [ -f "$ROOT/$path" ]; then
        prior=present
        cp -p "$ROOT/$path" "$BACKUP/before/$path"
        hash "$ROOT/$path" > "$BACKUP/before/$path.sha256"
    fi
    cp "$source" "$ROOT/$path.tic80-new.$$"
    chmod "$mode" "$ROOT/$path.tic80-new.$$"
    [ "$(hash "$ROOT/$path.tic80-new.$$")" = "$expected" ] || die "Staging checksum failed: $path"
    printf '%s\t%s\t%s\n' "$path" "$prior" "$expected" >> "$BACKUP/changes.tsv"
    mv -f "$ROOT/$path.tic80-new.$$" "$ROOT/$path"
}
for path in "${PATHS[@]}"; do
    case "$path" in *.rbf|*.pem) mode=0644 ;; *) mode=0755 ;; esac
    promote "$path" "$BUNDLE/payload/$path" "$mode"
done
if [ ! -f "$ROOT/$MASTER" ]; then
    promote "$MASTER" "$BUNDLE/frontier/Master_Daemon.sh" 0755
fi
# Existing shared Frontier code is never upgraded or restarted by this installer.
chmod 0755 "$ROOT/$MASTER"
mkdir -p "$ROOT/linux" "$ROOT/games/TIC-80/Carts" "$ROOT/saves/TIC-80" "$ROOT/logs/TIC-80"
if [ -f "$ROOT/$STARTUP" ]; then
    cp -p "$ROOT/$STARTUP" "$STAGE/startup"
elif [ -f "$ROOT/linux/_user-startup.sh" ]; then
    cp -p "$ROOT/linux/_user-startup.sh" "$STAGE/startup"
else
    printf '#!/bin/sh\n' > "$STAGE/startup"
fi
if ! sed '/^[[:space:]]*#/d' "$STAGE/startup" | grep -qF '/MiSTer_Frontier/Master_Daemon.sh'; then
    # Place the registration before any existing early exit. Preserve every
    # original startup line and avoid touching unrelated core registrations.
    {
        if head -n 1 "$STAGE/startup" | grep -q '^#!'; then
            head -n 1 "$STAGE/startup"
            printf '# MiSTer Frontier -- hybrid core master daemon\nbash /media/fat/MiSTer_Frontier/Master_Daemon.sh &\n'
            tail -n +2 "$STAGE/startup"
        else
            printf '#!/bin/sh\n# MiSTer Frontier -- hybrid core master daemon\nbash /media/fat/MiSTer_Frontier/Master_Daemon.sh &\n'
            cat "$STAGE/startup"
        fi
    } > "$STAGE/registered"
    mv "$STAGE/registered" "$STAGE/startup"
fi
bash -n "$STAGE/startup"
promote "$STARTUP" "$STAGE/startup" 0755
unchanged_main
sync
touch "$BACKUP/complete"
COMMITTED=1

echo "TIC-80 installed. Backup: $BACKUP"
echo 'MiSTer executable, MiSTer.ini, carts, saves and controller mappings preserved.'
if [ "$ROOT" = /media/fat ]; then
    # Use /proc argv tokens, not a substring search that matches this installer.
    count=0
    for cmdline in /proc/[0-9]*/cmdline; do
        [ -r "$cmdline" ] || continue
        if tr '\0' '\n' < "$cmdline" 2>/dev/null | grep -qxF "/media/fat/$MASTER"; then
            count=$((count + 1))
        fi
    done
    if [ "$count" = 0 ] && [ "$(cat /tmp/CORENAME 2>/dev/null || true)" = MENU ]; then
        nohup bash "$ROOT/$MASTER" </dev/null >/dev/null 2>&1 &
        sleep 1
        kill -0 "$!" 2>/dev/null || die 'Frontier failed to start; check logs/MiSTer_Frontier'
        echo 'Frontier started. Select TIC80 from the cores menu.'
    elif [ "$count" = 1 ]; then
        echo 'Existing Frontier is running. Select TIC80 from the cores menu.'
    else
        echo 'Reboot to activate Frontier before loading TIC80.'
    fi
fi
