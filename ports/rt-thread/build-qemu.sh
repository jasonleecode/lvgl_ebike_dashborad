#!/usr/bin/env bash
set -eu

port=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$port/../.." && pwd)
export RTT_ROOT="$root/os/rt-thread"
bsp="$RTT_ROOT/bsp/qemu-vexpress-a9"
out="$root/build/rt-thread"

if [[ ! -f "$bsp/SConstruct" || ! -f "$root/lvgl/SConscript" ]]; then
    echo 'Initialize dependencies with: git submodule update --init lvgl os/rt-thread' >&2
    exit 1
fi

# The BSP's SCons scripts generate files in their working tree. Stage the
# board in build/ and use the project's application in place of its demo.
map_build="$root/build/rt-map-tools"
cmake -S "$root/tools/map" -B "$map_build"
cmake --build "$map_build" --target ebike_map_data --parallel
mkdir -p "$out/applications"
ln -sfn "$root/app" "$out/applications/app"
cp -p "$bsp"/{SConstruct,SConscript,rtconfig.py,link.lds} "$out/"
cp -Rp "$bsp/drivers" "$out/"
cat "$port/qemu_config.h" "$bsp/rtconfig.h" > "$out/rtconfig.h"
cp -p "$port"/{SConscript,dashboard.c,platform_rtthread.c,lv_conf.h} "$out/applications/"
cp -p "$bsp/applications/main.c" "$out/applications/"
cp -p "$bsp/applications/lvgl/lv_port_indev.c" "$out/applications/"
ln -sfn "$root/lvgl" "$out/applications/lvgl"
ln -sfn "$root/ui" "$out/applications/ui"
exec scons -C "$out" "$@"
