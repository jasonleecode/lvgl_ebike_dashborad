#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lv_drivers/sdl/sdl.h"
#include "lvgl/src/misc/lv_mem.h"
#include "ui.h"
#include <SDL2/SDL.h>

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define LV_HOR_RES_MAX          (480)
#define LV_VER_RES_MAX          (272)


void hal_init(void); // 从模拟器模板复制

/* Internal functions */
static void print_lvgl_version(void);
static void print_usage(void);

void my_log_cb(const char *buf) {
    fprintf(stderr, "LVGL: %s", buf);
}

/**
 * @brief Print LVGL version
 */
static void print_lvgl_version(void)
{
    fprintf(stdout, "%d.%d.%d-%s\n",
            LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR,
            LVGL_VERSION_PATCH,
            LVGL_VERSION_INFO);
}

/**
 * @brief Print usage information
 */
static void print_usage(void)
{
    fprintf(stdout, "\nlvglsim [-V] [-B] [-b backend_name] [-W window_width] [-H window_height]\n\n");
    fprintf(stdout, "-V print LVGL version\n");
    fprintf(stdout, "-B list supported backends\n");
}

void hal_init(void)
{
    /* 初始化 SDL 显示 */
    sdl_init();
    print_lvgl_version();

    /* 初始化 LVGL 显示缓冲区 */
    static lv_color_t buf1[LV_HOR_RES_MAX * LV_VER_RES_MAX], buf2[LV_HOR_RES_MAX * LV_VER_RES_MAX];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LV_HOR_RES_MAX * LV_VER_RES_MAX);

    /* 注册显示驱动 */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
    if (disp == NULL) {
        printf("Display driver registration failed!\n");
    // 错误处理
    } else {
        printf("Display driver registered successfully!\n");
    }

    // 初始化输入设备（关键！）
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;  // 确保实现此回调函数
    lv_indev_drv_register(&indev_drv);
}

int main(void)
{
    lv_log_register_print_cb(my_log_cb);  // 注册日志回调
    /* Initialize LVGL. */
    lv_init();
    hal_init();       // 初始化显示和输入设备（SDL）
    ui_init();        // SquareLine 生成的 UI 初始化函数

    while (1) {   
        lv_tick_inc(5);  // 模拟 5ms 间隔
        lv_task_handler();
        lv_timer_handler();  // LVGL 事件循环
        SDL_Delay(5);        // 避免 CPU 占用过高
    }

    return 0;
}
