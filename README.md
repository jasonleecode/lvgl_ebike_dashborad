# LVGL 电动自行车仪表盘

同一套 480 × 272 的 SquareLine 界面可以在 Linux PC 上通过 SDL2 运行，也可以编译成 RT-Thread 固件，在 QEMU 的 Cortex-A9 参考板上运行。两个目标都使用仓库锁定的 LVGL 8.3.11。以下命令从仓库根目录执行。

![仪表盘](https://github.com/user-attachments/assets/178d5030-8ab9-4fb4-8e9e-0d64d2b9ba51)

## Linux PC

需要 C 编译器、CMake、pkg-config 和 SDL2 开发包（Ubuntu 的 `libsdl2-dev`）。

```sh
git submodule update --init --depth 1 lvgl lv_drivers
cmake -S . -B build
cmake --build build --parallel
./build/ebike_sim
```

## RT-Thread

参考目标为 `qemu-vexpress-a9`。Linux 主机需要 Python 3、SCons、含 newlib 和 libsupc++ 的 Arm 裸机 GCC 工具链，以及 `qemu-system-arm`。Ubuntu 上对应的软件包为 `scons`、`gcc-arm-none-eabi`、`libnewlib-arm-none-eabi`、`libstdc++-arm-none-eabi-newlib` 和 `qemu-system-arm`。

```sh
git submodule update --init --depth 1 lvgl os/rt-thread
./ports/rt-thread/build-qemu.sh -j8
qemu-system-arm -M vexpress-a9 -m 128M -kernel build/rt-thread/rtthread.elf -serial stdio
```

固件启动后会在 QEMU 的 LCD 窗口显示仪表盘，用鼠标点击和拖动即可操作。终端显示 RT-Thread 启动日志及 MSH 命令行。若工具链安装在自定义目录，可在构建前设置 `RTT_EXEC_PATH` 指向包含 `arm-none-eabi-gcc` 的目录。

构建脚本在 `build/rt-thread/` 下准备板级工程。RT-Thread 提供时钟、堆和 LVGL 工作线程，板级包提供 PL111 LCD 与 PL050 鼠标驱动。应用在 `lv_user_gui_init()` 中加载 `ui/`，显示回调把 32 位像素转换为 LCD 使用的 RGB565。

## STM32

STM32 实板适配仍待完成。移植到其他已运行 LVGL 8.3 的 RT-Thread BSP 时，可按 `ui/filelist.txt` 加入界面源码，在 BSP 注册显示器和输入设备后，从 LVGL 线程调用 `ui_init()`。现有素材要求 `LV_COLOR_DEPTH=32`、`LV_COLOR_16_SWAP=0`，并使用 basic 主题。
