#include "telemetry.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bike_readings live;
static uint32_t fix_ms, speed_ms, battery_ms, temp_ms;
static double previous_utc = -1, previous_speed;
static const uint32_t gps_timeout = 5000;

static bool number(const char *text, double min, double max, double *result)
{
    const char *digits = text + (*text == '+' || *text == '-');
    size_t whole = strspn(digits, "0123456789");
    const char *rest = digits + whole;
    if (!whole) return false;
    if (*rest == '.') {
        ++rest;
        rest += strspn(rest, "0123456789");
    }
    if (*rest) return false;
    char *end;
    double value = strtod(text, &end);
    if (text == end || *end || !isfinite(value) || value < min || value > max) return false;
    *result = value;
    return true;
}

static bool coordinate(const char *field, const char *hemisphere, bool latitude, double *result)
{
    double raw;
    int digits = latitude ? 4 : 5;
    const char *directions = latitude ? "NS" : "EW";
    if (!hemisphere[0] || hemisphere[1] || !strchr(directions, hemisphere[0]) ||
        strspn(field, "0123456789") != (size_t)digits || !number(field, 0, 18000, &raw)) return false;
    int degrees = (int)(raw / 100);
    double minutes = raw - 100 * degrees;
    double value = degrees + minutes / 60;
    if (minutes >= 60 || value > (latitude ? 90 : 180)) return false;
    *result = hemisphere[0] == directions[1] ? -value : value;
    return true;
}

static void update_trip(const char *time, double speed)
{
    double utc;
    if (strspn(time, "0123456789") != 6 || !number(time, 0, 235959.999999, &utc)) return;
    int hour = (int)utc / 10000, minute = (int)utc / 100 % 100;
    double second = fmod(utc, 100);
    if (hour > 23 || minute > 59 || second >= 60) return;
    utc = hour * 3600 + minute * 60 + second;
    double elapsed = fmod(utc - previous_utc + 86400, 86400);
    if (previous_utc >= 0 && elapsed > 0 && elapsed <= gps_timeout / 1000) {
        live.trip_seconds += elapsed;
        live.trip_km += (previous_speed + speed) * 0.5 * elapsed / 3600;
    }
    live.trip_started = true;
    if (speed > live.max_kmh) live.max_kmh = speed;
    previous_utc = utc;
    previous_speed = speed;
}

/* GPS course over ground. Where a receiver only supplies fixes, accumulate
 * three metres of movement before changing direction to limit stationary jitter. */
static void update_course(double lat, double lon, bool continuous, bool stopped, const char *course)
{
    static double anchor_lat, anchor_lon;
    double angle;
    if (!continuous) live.course_valid = false;
    if (!stopped && course && number(course, 0, 360, &angle)) {
        live.course_deg = angle == 360 ? 0 : angle;
        live.course_valid = true;
    } else if (continuous && !stopped) {
        double east = remainder(lon - anchor_lon, 360.0) * cos(lat * 0.017453292519943295);
        double north = lat - anchor_lat;
        double threshold = 3.0 / 111195.0;
        if (east*east + north*north < threshold*threshold) return;
        angle = atan2(east, north) * 57.29577951308232;
        live.course_deg = angle < 0 ? angle + 360 : angle;
        live.course_valid = true;
    }
    anchor_lat = lat;
    anchor_lon = lon;
}

static void sentence(char *line, uint32_t now)
{
    char *star = strchr(line, '*');
    if (!star || strlen(star + 1) != 2 || !isxdigit((unsigned char)star[1]) ||
        !isxdigit((unsigned char)star[2])) return;
    unsigned checksum = 0;
    for (char *p = line; p < star; ++p) checksum ^= (unsigned char)*p;
    if (checksum != strtoul(star+1, NULL, 16)) return;
    *star = 0;
    char *fields[24] = {line};
    size_t count = 1;
    for (char *p = line; *p; ++p) {
        if (*p == ',') {
            if (count == sizeof(fields)/sizeof(fields[0])) return;
            *p = 0;
            fields[count++] = p + 1;
        }
    }

    double value;
    if (!strcmp(fields[0], "PEBIK") && count == 3) {
        live.temp_valid = number(fields[1], -100, 150, &value);
        if (live.temp_valid) live.temp_deci_c = (int)lround(value * 10);
        live.battery_valid = number(fields[2], 0, 100, &value);
        if (live.battery_valid) live.battery_pct = (int)lround(value);
        temp_ms = battery_ms = now;
        return;
    }
    if (strlen(fields[0]) != 5 || count < 8) return;
    bool rmc = !strcmp(fields[0]+2, "RMC");
    if (!rmc && strcmp(fields[0]+2, "GGA")) return;
    int i = rmc ? 3 : 2;
    double lat, lon;
    bool valid;
    if (rmc) {
        valid = !strcmp(fields[2], "A") && (count <= 12 || strcmp(fields[12], "N"));
    } else {
        valid = fields[6][0] >= '1' && fields[6][0] <= '5' && !fields[6][1];
    }
    if (!valid || !coordinate(fields[i], fields[i+1], true, &lat) ||
        !coordinate(fields[i+2], fields[i+3], false, &lon)) {
        live.fix = live.speed_valid = live.course_valid = false;
        previous_utc = -1;
        return;
    }
    bool continuous = live.fix && now - fix_ms <= gps_timeout;
    live.latitude = lat;
    live.longitude = lon;
    live.fix = true;
    fix_ms = now;
    if (rmc) {
        if (now - speed_ms > gps_timeout) previous_utc = -1;
        live.speed_valid = number(fields[7], 0, 1000, &value);
        if (live.speed_valid) {
            live.speed_kmh = value * 1.852;
            speed_ms = now;
            update_trip(fields[1], live.speed_kmh);
        } else previous_utc = -1;
    }
    bool stopped = live.speed_valid && now - speed_ms <= gps_timeout && live.speed_kmh < 1.0;
    update_course(lat, lon, continuous, stopped, rmc && count > 8 ? fields[8] : NULL);
}

void telemetry_feed(const char *bytes, size_t count, uint32_t now_ms)
{
    static char line[128];
    static size_t length;
    static bool collecting;
    for (size_t i = 0; i < count; ++i) {
        char c = bytes[i];
        if (c == '$') { collecting = true; length = 0; }
        else if (collecting && (c == '\r' || c == '\n')) {
            line[length] = 0;
            sentence(line, now_ms);
            collecting = false;
        } else if (collecting) {
            if (length < sizeof(line)-1 && c >= 32 && c < 127) line[length++] = c;
            else collecting = false;
        }
    }
}

void telemetry_poll(uint32_t now_ms, bike_readings *result)
{
    if (now_ms - fix_ms > gps_timeout) live.fix = live.course_valid = false;
    if (now_ms - speed_ms > gps_timeout) { live.speed_valid = false; previous_utc = -1; }
    if (now_ms - battery_ms > 10000) live.battery_valid = false;
    if (now_ms - temp_ms > 10000) live.temp_valid = false;
    *result = live;
    if (platform_temperature(&result->temp_deci_c)) result->temp_valid = true;
    if (platform_battery(&result->battery_pct)) result->battery_valid = true;
}
