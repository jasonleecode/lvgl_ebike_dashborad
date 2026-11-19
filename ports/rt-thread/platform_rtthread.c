#include <math.h>
#include <rtdevice.h>
#include <sys/time.h>
#include "app/telemetry.h"
#include "app/mapengine/map_platform.h"
#ifdef SOC_VEXPRESS_A9
#include "realview.h"
#include <dfs_fs.h>
#endif

#ifndef EBIKE_GPS_UART
#define EBIKE_GPS_UART "uart1"
#endif
#ifndef EBIKE_GPS_BAUD
#define EBIKE_GPS_BAUD 9600
#endif
#ifndef EBIKE_TEMP_DEVICE
#define EBIKE_TEMP_DEVICE "temp0"
#endif

static rt_device_t gps;

void platform_start(void)
{
#ifdef SOC_VEXPRESS_A9
    /* Seed the BSP's software RTC from the emulated PL031 data register. */
    struct timeval tv = {.tv_sec = *(volatile const uint32_t *)REALVIEW_RTC_BASE};
    settimeofday(&tv, RT_NULL);
#endif
    rt_device_t dev = rt_device_find(EBIKE_GPS_UART);
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = EBIKE_GPS_BAUD;
    cfg.bufsz = 1024;
    if (dev && rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, &cfg) == RT_EOK &&
        rt_device_open(dev, RT_DEVICE_FLAG_RDONLY | RT_DEVICE_FLAG_INT_RX) == RT_EOK)
        gps = dev;
}

size_t platform_read_serial(char *buf, size_t size)
{
    rt_ssize_t n = gps ? rt_device_read(gps, 0, buf, size) : 0;
    return n > 0 ? (size_t)n : 0;
}

bool platform_temperature(int *value)
{
#ifdef RT_USING_SENSOR
    static rt_device_t temp;
    if (!temp) {
        rt_device_t dev = rt_device_find(EBIKE_TEMP_DEVICE);
        if (dev && dev->type == RT_Device_Class_Sensor &&
            rt_device_open(dev, RT_DEVICE_FLAG_RDONLY) == RT_EOK) temp = dev;
    }
    struct rt_sensor_data data;
    if (temp && rt_device_read(temp, 0, &data, 1) == 1) {
        rt_sensor_t sensor = (rt_sensor_t)temp;
#ifdef RT_USING_SENSOR_V2
        if (data.type == RT_SENSOR_TYPE_TEMP && sensor->info.unit == RT_SENSOR_UNIT_CELSIUS &&
            data.data.temp >= -100 && data.data.temp <= 150) {
            *value = (int)lround(data.data.temp * 10);
            return true;
        }
#else
        if (data.type == RT_SENSOR_CLASS_TEMP && sensor->info.unit == RT_SENSOR_UNIT_DCELSIUS &&
            data.data.temp >= -1000 && data.data.temp <= 1500) {
            *value = data.data.temp;
            return true;
        }
#endif
    }
#endif
    (void)value;
    return false;
}

/* A board with a native BMS driver may supply this instead of serial SOC. */
rt_weak bool platform_battery(int *value)
{
    (void)value;
    return false;
}

#ifndef EBIKE_MAP_PATH
#define EBIKE_MAP_PATH "/beijing.map"
#endif

const char *platform_map_path(void)
{
#ifdef SOC_VEXPRESS_A9
    /* The emulator image is a FAT volume on the whole SD device. Real boards
     * mount their own volume and select EBIKE_MAP_PATH for their layout. */
    static bool mounted;
    if (!mounted && rt_device_find("sd"))
        mounted = dfs_mount("sd", "/", "elm", MS_RDONLY, RT_NULL) == 0;
#endif
    return EBIKE_MAP_PATH;
}
