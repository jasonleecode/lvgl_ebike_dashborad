#ifndef EBIKE_TELEMETRY_H
#define EBIKE_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    double latitude, longitude, course_deg;
    double speed_kmh, max_kmh, trip_km;
    double trip_seconds;
    int battery_pct, temp_deci_c;
    bool fix, course_valid, speed_valid, trip_started, battery_valid, temp_valid;
} bike_readings;

void telemetry_feed(const char *bytes, size_t count, uint32_t now_ms);
void telemetry_poll(uint32_t now_ms, bike_readings *result);
void platform_start(void);
size_t platform_read_serial(char *buf, size_t size);
bool platform_temperature(int *deci_c);
bool platform_battery(int *percent);

#endif
