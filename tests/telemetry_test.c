/* cc -std=c11 -Iapp tests/telemetry_test.c app/telemetry.c -lm -o build/telemetry_test */
#include "telemetry.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

bool platform_temperature(int *value) { (void)value; return false; }
bool platform_battery(int *value) { (void)value; return false; }

static void send_sentence(const char *body, uint32_t now)
{
    char line[128];
    unsigned checksum = 0;
    for (const char *p = body; *p; ++p) checksum ^= (unsigned char)*p;
    int len = snprintf(line, sizeof(line), "$%s*%02X\r\n", body, checksum);
    assert(len > 0 && (size_t)len < sizeof(line));
    for (int i = 0; i < len; ++i) telemetry_feed(&line[i], 1, now);
}

int main(void)
{
    bike_readings r;
    uint32_t before_wrap = UINT32_MAX - 999;
    send_sentence("GNRMC,235959,A,3954.4200,N,11623.8200,E,10,,181126,,,A", before_wrap);
    send_sentence("GPRMC,000000,A,3954.4200,N,11623.8200,E,10,,191126,,,A", 0);
    send_sentence("PEBIK,-3.5,0", 0);
    telemetry_poll(1, &r);
    assert(r.fix && r.speed_valid && fabs(r.latitude-39.907) < 0.00001);
    assert(fabs(r.longitude-116.397) < 0.00001 && fabs(r.speed_kmh-18.52) < 0.001);
    assert(r.trip_seconds == 1 && fabs(r.trip_km-18.52/3600) < 0.000001);
    assert(r.temp_valid && r.temp_deci_c == -35 && r.battery_valid && r.battery_pct == 0);

    const char corrupt[] = "$PEBIK,99,99*00\r\n";
    telemetry_feed(corrupt, strlen(corrupt), 100);
    telemetry_poll(101, &r);
    assert(r.temp_deci_c == -35 && r.battery_pct == 0);
    send_sentence("GNRMC,000001,V,,,,,,,191126,,,N", 1000);
    telemetry_poll(1000, &r);
    assert(!r.fix && !r.speed_valid);

    send_sentence("GNGGA,000002,3350.0,S,15112.0,W,1,9,1.2,0,M,0,M,,", 2000);
    telemetry_poll(2000, &r);
    assert(r.fix && r.latitude < 0 && r.longitude < 0 && !r.speed_valid);
    telemetry_poll(8000, &r);
    assert(!r.fix && r.battery_valid);
    telemetry_poll(11000, &r);
    assert(!r.battery_valid && !r.temp_valid);
    send_sentence("PEBIK,,100", 12000);
    telemetry_poll(12000, &r);
    assert(!r.temp_valid && r.battery_valid && r.battery_pct == 100);
    send_sentence("GPRMC,000013,A,3961.0,N,11623.8200,E,10,,191126,,,A", 13000);
    telemetry_poll(13000, &r);
    assert(!r.fix && r.trip_seconds == 1);
    send_sentence("GNRMC,000014,A,3954.4200,N,11623.8200,E,5,90,191126,,,A", 14000);
    telemetry_poll(14000, &r);
    assert(r.course_valid && r.course_deg == 90);
    send_sentence("GNRMC,000015,A,3954.4200,N,11623.8200,E,0,270,191126,,,A", 15000);
    telemetry_poll(15000, &r);
    assert(r.course_deg == 90);
    send_sentence("GNRMC,000016,A,3954.4100,N,11623.8200,E,5,,191126,,,A", 16000);
    telemetry_poll(16000, &r);
    assert(r.course_valid && fabs(r.course_deg-180) < 0.01);
    telemetry_poll(22000, &r);
    assert(!r.fix && !r.course_valid);
    send_sentence("GNGGA,000023,3954.5000,N,11623.8200,E,1,9,1.2,0,M,0,M,,", 23000);
    telemetry_poll(23000, &r);
    assert(r.fix && !r.course_valid);
    puts("Telemetry tests passed");
}
