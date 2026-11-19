#include "mapengine/map_platform.h"
#include "telemetry.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static const char *gps_path, *temp_path, *battery_path;
static speed_t baud = B9600;
static int gps = -1;

void platform_start(void)
{
    gps_path = getenv("EBIKE_GPS");
    temp_path = getenv("EBIKE_TEMP");
    battery_path = getenv("EBIKE_BATTERY");
    const char *rate = getenv("EBIKE_BAUD");
    if (rate) {
        static const struct { const char *name; speed_t speed; } rates[] = {
            {"4800", B4800}, {"9600", B9600}, {"19200", B19200}, {"38400", B38400},
            {"57600", B57600}, {"115200", B115200}
        };
        size_t i = 0;
        while (i < sizeof(rates)/sizeof(rates[0]) && strcmp(rate, rates[i].name)) ++i;
        if (i < sizeof(rates)/sizeof(rates[0])) baud = rates[i].speed;
        else { fprintf(stderr, "Unsupported EBIKE_BAUD: %s\n", rate); gps_path = NULL; }
    }

}

size_t platform_read_serial(char *buf, size_t size)
{
    static time_t retry;
    static bool regular_file;
    if (!gps_path) return 0;
    if (gps < 0) {
        time_t now = time(NULL);
        if (now == retry) return 0;
        retry = now;
        gps = open(gps_path, O_RDONLY | O_NONBLOCK | O_NOCTTY);
        if (gps < 0) return 0;
        struct stat st;
        regular_file = fstat(gps, &st) == 0 && S_ISREG(st.st_mode);
        if (isatty(gps)) {
            struct termios attr;
            if (tcgetattr(gps, &attr) < 0) goto disconnected;
            cfmakeraw(&attr);
            attr.c_cflag |= CLOCAL | CREAD;
            cfsetispeed(&attr, baud);
            cfsetospeed(&attr, baud);
            if (tcsetattr(gps, TCSANOW, &attr) < 0) goto disconnected;
        }
    }
    ssize_t count = read(gps, buf, size);
    if (count > 0) return (size_t)count;
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
    if (count == 0 && regular_file) gps_path = NULL; /* Read a capture only once. */
disconnected:
    close(gps);
    gps = -1;
    return 0;
}

static bool read_int(const char *path, int min, int max, int *value)
{
    if (!path) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    long number;
    char extra;
    bool ok = fscanf(f, "%ld %c", &number, &extra) == 1 && number >= min && number <= max;
    fclose(f);
    if (ok) *value = number;
    return ok;
}

bool platform_temperature(int *value)
{
    int milli;
    if (!read_int(temp_path, -100000, 150000, &milli)) return false;
    *value = milli / 100;
    return true;
}

bool platform_battery(int *value)
{
    return read_int(battery_path, 0, 100, value);
}

const char *platform_map_path(void)
{
    const char *path = getenv("EBIKE_MAP_FILE");
    return path && *path ? path : EBIKE_MAP_PATH;
}
