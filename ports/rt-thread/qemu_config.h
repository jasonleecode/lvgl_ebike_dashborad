/* Add the console, LCD and mouse used by the dashboard to the BSP defaults. */
#define BSP_USING_UART0
#define BSP_DRV_CLCD
#define BSP_LCD_WIDTH 480
#define BSP_LCD_HEIGHT 272
#define RT_USING_LCD
#define BSP_DRV_MOUSE
#define PKG_USING_LVGL
#define PKG_LVGL_THREAD_STACK_SIZE 8192

/* Kconfig settings missing from the pinned BSP's generated rtconfig.h.
 * The MMU uses the default page affinity and SDIO selects the block layer. */
#define RT_PAGE_AFFINITY_BLOCK_SIZE 0x1000
#define RT_USING_BLK
