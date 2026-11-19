#include <rtdevice.h>
#include "lvgl.h"
#include "app/application.h"

static struct rt_device_graphic_info lcd_info;
static lv_color_t pixels[BSP_LCD_WIDTH * 16];
static lv_disp_draw_buf_t draw_buffer;
static lv_disp_drv_t display;

static void flush_rgb565(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *src)
{
    uint16_t *fb = (uint16_t *)lcd_info.framebuffer;
    for (int y = area->y1; y <= area->y2; ++y) {
        uint16_t *dst = fb + y * lcd_info.width + area->x1;
        for (int x = area->x1; x <= area->x2; ++x) {
            *dst++ = lv_color_to16(*src++);
        }
    }
    lv_disp_flush_ready(driver);
}

void lv_port_disp_init(void)
{
    rt_device_t lcd = rt_device_find("lcd");
    if (!lcd || rt_device_open(lcd, RT_DEVICE_OFLAG_RDWR) != RT_EOK) {
        rt_kprintf("Dashboard: cannot open lcd\n");
        return;
    }

    if (rt_device_control(lcd, RTGRAPHIC_CTRL_GET_INFO, &lcd_info) != RT_EOK ||
        !lcd_info.framebuffer || lcd_info.pixel_format != RTGRAPHIC_PIXEL_FORMAT_RGB565 ||
        lcd_info.bits_per_pixel != 16 || lcd_info.width != BSP_LCD_WIDTH ||
        lcd_info.height != BSP_LCD_HEIGHT) {
        rt_kprintf("Dashboard: incompatible LCD framebuffer\n");
        rt_device_close(lcd);
        return;
    }

    lv_disp_draw_buf_init(&draw_buffer, pixels, NULL, sizeof(pixels) / sizeof(pixels[0]));
    lv_disp_drv_init(&display);
    display.hor_res = lcd_info.width;
    display.ver_res = lcd_info.height;
    display.draw_buf = &draw_buffer;
    display.flush_cb = flush_rgb565;
    if (!lv_disp_drv_register(&display)) {
        rt_kprintf("Dashboard: display registration failed\n");
        rt_device_close(lcd);
    }
}

/* Called by LVGL's RT-Thread thread after display and input registration. */
void lv_user_gui_init(void)
{
    if (lv_disp_get_default()) {
        dashboard_start();
        rt_kprintf("E-bike dashboard ready (%dx%d)\n", lcd_info.width, lcd_info.height);
    }
}
