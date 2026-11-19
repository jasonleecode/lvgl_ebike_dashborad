#!/usr/bin/env bash
# Create a QEMU FAT image in the build tree. No physical device is opened.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
dir="$root/build/rt-map-tools"
work=$(mktemp -d "$dir/sd.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkfs.fat -F 16 -s 16 -C "$work/map-sd.img" 65536
mcopy -i "$work/map-sd.img" "$dir/beijing.map" ::beijing.map
mv "$work/map-sd.img" "$dir/map-sd.img"
