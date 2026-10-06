#!/bin/sh
# Offline parity/performance qualification; does not select a core or emit input.
set -eu
if [ "$#" -ne 3 ]; then
    echo 'Usage: bench_languages.sh player cartridge_directory output_directory' >&2
    exit 2
fi
player=$1
carts=$2
output=$3
mkdir -p "$output"
for language in lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth; do
    echo "LANGUAGE $language"
    "$player" "$carts/$language.tic" 600 "$output/$language.rgba" "$output/$language.s16le" 8
    sha256sum "$output/$language.rgba" "$output/$language.s16le"
done
