#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "application.h"
#include "map_view.h"
#include "ui/ui.h"

static void label(lv_obj_t *obj, const char *text)
{
    if (strcmp(lv_label_get_text(obj), text)) lv_label_set_text(obj, text);
}

static void update_display(lv_timer_t *timer)
{
    (void)timer;
    bike_readings r;
    telemetry_poll(lv_tick_get(), &r);
    char text[48];
    struct tm tm;
    time_t now = time(NULL);
    const char *period = "";
    strcpy(text, "--:--");
    if (now > 0 && localtime_r(&now, &tm)) {
        int hour = tm.tm_hour % 12;
        snprintf(text, sizeof(text), "%02d:%02d", hour ? hour : 12, tm.tm_min);
        period = tm.tm_hour >= 12 ? "PM" : "AM";
    }
    label(ui_Label_Time3, text); label(ui_Label_Time2, text);
    label(ui_LabeL_PM2, period); label(ui_LabeL_PM1, period);
    strcpy(text, "--");
    if (r.temp_valid) snprintf(text, sizeof(text), "%+.1f", r.temp_deci_c/10.0);
    label(ui_Label_Temp2, text); label(ui_Label_Temp1, text);

    strcpy(text, "--");
    if (r.battery_valid) snprintf(text, sizeof(text), "%d", r.battery_pct);
    label(ui_Label_Battery_Number, text); label(ui_Label_Charging_Number, text);
    lv_slider_set_value(ui_Slider_Battery, r.battery_valid ? r.battery_pct : 0, LV_ANIM_OFF);

    int wave_y = 120 - (r.battery_valid ? r.battery_pct * 120 / 100 : 0);
    lv_obj_set_y(ui_Wave1, wave_y);
    lv_obj_set_y(ui_Wave2, wave_y);
    if (r.battery_valid) lv_obj_clear_flag(ui_Wave_Group, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ui_Wave_Group, LV_OBJ_FLAG_HIDDEN);

    /* The large SquareLine speed font only contains digits. */
    const lv_font_t *font = r.speed_valid ? &ui_font_Number : &ui_font_Big;
    if (lv_obj_get_style_text_font(ui_Speed_Number_1, 0) != font) {
        lv_obj_set_style_text_font(ui_Speed_Number_1, font, 0);
        lv_obj_set_style_text_font(ui_Speed_Number_2, font, 0);
    }
    strcpy(text, "--");
    if (r.speed_valid) snprintf(text, sizeof(text), "%ld", lround(r.speed_kmh));
    label(ui_Speed_Number_1, text); label(ui_Speed_Number_2, r.speed_valid ? text : "");
    lv_slider_set_value(ui_Slider_Speed, r.speed_valid ? lround(r.speed_kmh) : 0, LV_ANIM_OFF);
    lv_chart_series_t *series = lv_chart_get_series_next(ui_Chart2, NULL);
    lv_chart_set_next_value(ui_Chart2, series, r.speed_valid ? lround(r.speed_kmh) : LV_CHART_POINT_NONE);

    strcpy(text, "--");
    if (r.trip_started) snprintf(text, sizeof(text), "%.1f", r.trip_km);
    label(ui_Label_Trip_Number, text);
    if (r.trip_started) snprintf(text, sizeof(text), "%.1f", r.max_kmh);
    label(ui_Label_Max_Speed_Number, text); label(ui_Label_Max_Speed_Number1, text);
    strcpy(text, "--");
    if (r.trip_seconds > 0) snprintf(text, sizeof(text), "%.1f", r.trip_km * 3600 / r.trip_seconds);
    label(ui_Label_AVG_Speed_Number, text); label(ui_Label_AVG_Speed_Number1, text);
    unsigned seconds = (unsigned)r.trip_seconds;
    snprintf(text, sizeof(text), "%02u:%02u:%02u", seconds/3600, seconds/60%60, seconds%60);
    label(ui_Label_Arrival_Time_Number1, text);
    map_view_update(&r);
}

static void read_serial(lv_timer_t *timer)
{
    (void)timer;
    char bytes[256];
    /* Bound the work per LVGL tick even with a continuously busy UART. */
    for (int i = 0; i < 4; ++i) {
        size_t count = platform_read_serial(bytes, sizeof(bytes));
        telemetry_feed(bytes, count, lv_tick_get());
        if (count < sizeof(bytes)) break;
    }
}

void dashboard_start(void)
{
    platform_start();
    ui_init();
    map_view_init();
    lv_obj_clear_flag(ui_Slider_Speed, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui_Slider_Battery, LV_OBJ_FLAG_CLICKABLE);
    lv_slider_set_range(ui_Slider_Battery, 0, 100);
    lv_obj_add_flag(ui_Image_Flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(ui_Label_Arrival_Time_Number1, &ui_font_Medium, 0);
    label(ui_LabeL_Charging, "CHARGE");
    label(ui_Label_ODO_Number, "--");
    label(ui_Label_ETA_Number, "--");
    label(ui_Label_Distance_Number, "--");
    label(ui_Label_Complete_Number, "--");
    lv_chart_set_all_value(ui_Chart2, lv_chart_get_series_next(ui_Chart2, NULL), LV_CHART_POINT_NONE);
    read_serial(NULL);
    update_display(NULL);
    lv_timer_create(read_serial, 50, NULL);
    lv_timer_create(update_display, 1000, NULL);
}
