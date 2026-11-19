#include <stdio.h>
#include <string.h>
#include "map_view.h"
#include "mapengine/mapengine.h"
#include "ui/ui.h"

enum { COMPACT_WIDTH = 200, FADE_WIDTH = 64, CONTROL_HEIGHT = 28 };
static lv_obj_t *view, *status, *marker, *scale_label, *size_caption, *zoom_in, *zoom_out, *heading_button, *road_button;
static double last_course;
static bool maximized, fixed, preview_moved, last_fix, map_available;

/* Apply the same horizontal opacity ramp to each scanline of the map. */
static lv_draw_mask_res_t fade_scanline(lv_opa_t *opacity, lv_coord_t x, lv_coord_t y,
                                        lv_coord_t length, void *param)
{
    (void)y;
    const lv_draw_mask_fade_param_t *fade = param;
    int offset = x - fade->cfg.coords.x1;
    if (offset >= FADE_WIDTH) return LV_DRAW_MASK_RES_FULL_COVER;
    for (int i = 0; i < length && offset < FADE_WIDTH; ++i, ++offset)
        opacity[i] = opacity[i] * LV_MAX(offset, 0) / FADE_WIDTH;
    return LV_DRAW_MASK_RES_CHANGED;
}

static void compact_edge(lv_event_t *event)
{
    static lv_draw_mask_fade_param_t fade = {
        .dsc = {.cb = fade_scanline, .type = LV_DRAW_MASK_TYPE_FADE}
    };
    static int16_t mask_id = LV_MASK_ID_INV;
    if (maximized) return;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_COVER_CHECK:
        if (lv_event_get_cover_area(event)->x1 < view->coords.x1 + FADE_WIDTH)
            lv_event_set_cover_res(event, LV_COVER_RES_MASKED);
        break;
    case LV_EVENT_DRAW_MAIN_BEGIN:
        lv_obj_get_coords(view, &fade.cfg.coords);
        if (lv_event_get_draw_ctx(event)->clip_area->x1 < fade.cfg.coords.x1 + FADE_WIDTH)
            mask_id = lv_draw_mask_add(&fade, NULL);
        break;
    case LV_EVENT_DRAW_MAIN_END:
        lv_draw_mask_remove_id(mask_id);
        mask_id = LV_MASK_ID_INV;
        break;
    default:
        break;
    }
}

static void update_fix_status(void)
{
    if (!lv_obj_is_visible(view)) return;
    bool available = mapengine_prepare(view);
    if (available != map_available) lv_obj_invalidate(view);
    map_available = available;
    char text[80];
    const char *message = "Map file unavailable";
    if (available && fixed) {
        mapengine_point_t position = mapengine_get_center(view);
        snprintf(text, sizeof(text), "%s  %.5f, %.5f", mapengine_has_coverage(view) ? "GPS" : "No map",
                 position.latitude, position.longitude);
        message = text;
    } else if (available) {
        message = preview_moved ? "Map preview  |  No GPS fix" :
                  last_fix ? "Last position  |  No GPS fix" : "Beijing preview  |  No GPS fix";
    }
    char detail[112];
    if (mapengine_is_road_view(view) && available) {
        snprintf(detail, sizeof(detail), "%s\nLane layout estimated", message);
        message = detail;
    }
    if (strcmp(lv_label_get_text(status),message)) lv_label_set_text(status,message);
}

static void drag_preview(lv_event_t *event)
{
    (void)event;
    if (fixed) return;
    lv_point_t delta;
    lv_indev_get_vect(lv_indev_get_act(), &delta);
    if (delta.x || delta.y) {
        mapengine_pan(view, delta.x, delta.y);
        if (!preview_moved) {
            preview_moved = true;
            update_fix_status();
        }
    }
}

static void toggle_heading(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    if (lv_obj_has_state(button, LV_STATE_CHECKED)) {
        lv_obj_clear_state(button, LV_STATE_CHECKED);
        mapengine_set_heading(view, 0);
    } else {
        lv_obj_add_state(button, LV_STATE_CHECKED);
        mapengine_set_heading(view, last_course);
    }
}

static void draw_vehicle(lv_event_t *event)
{
    if (!mapengine_is_road_view(view)) return;
    lv_area_t box;
    lv_obj_get_coords(marker, &box);
    lv_coord_t x = (box.x1+box.x2)/2, y = (box.y1+box.y2)/2;
    lv_point_t tip[] = {{x,y-10},{x+8,y+9},{x-8,y+9}};
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_white();
    lv_draw_polygon(lv_event_get_draw_ctx(event), &fill, tip, 3);
    lv_point_t body[] = {{x,y-6},{x+5,y+7},{x-5,y+7}};
    fill.bg_color = lv_color_hex(0x1599c8);
    lv_draw_polygon(lv_event_get_draw_ctx(event), &fill, body, 3);
}

static void sync_zoom_controls(void)
{
    int level = mapengine_get_zoom(view);
    lv_obj_clear_state(zoom_in, LV_STATE_DISABLED);
    lv_obj_clear_state(zoom_out, LV_STATE_DISABLED);
    if (level == MAPENGINE_MAX_ZOOM) lv_obj_add_state(zoom_in, LV_STATE_DISABLED);
    if (level == MAPENGINE_MIN_ZOOM) lv_obj_add_state(zoom_out, LV_STATE_DISABLED);

    int dm = (int)(10 * mapengine_get_scale_metres(view));
    char label[16];
    if (dm % 10) snprintf(label, sizeof(label), "%d.%d m", dm / 10, dm % 10);
    else snprintf(label, sizeof(label), "%d m", dm / 10);
    lv_label_set_text(scale_label, label);

    bool road_mode = mapengine_is_road_view(view);
    if (road_mode != lv_obj_has_state(road_button, LV_STATE_CHECKED)) {
        if (road_mode) {
            lv_obj_add_state(road_button, LV_STATE_CHECKED);
            lv_obj_add_state(heading_button, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(road_button, LV_STATE_CHECKED);
            lv_obj_clear_state(heading_button, LV_STATE_DISABLED);
        }
        bool follow = road_mode || lv_obj_has_state(heading_button, LV_STATE_CHECKED);
        mapengine_set_heading(view, follow ? last_course : 0);
        lv_obj_set_size(marker, road_mode ? 19 : 11, road_mode ? 23 : 11);
        lv_obj_align(marker, LV_ALIGN_CENTER, 0, road_mode ? lv_pct(25) : 0);
        lv_obj_set_style_bg_opa(marker, road_mode ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(marker, road_mode ? 0 : 2, 0);
    }
    update_fix_status();
}

static void zoom(lv_event_t *event)
{
    int step = lv_event_get_target(event) == zoom_in ? 1 : -1;
    mapengine_set_zoom(view, mapengine_get_zoom(view) + step);
    sync_zoom_controls();
}

static void toggle_road_view(lv_event_t *event)
{
    (void)event;
    mapengine_set_road_view(view, !mapengine_is_road_view(view));
    sync_zoom_controls();
}

static void set_map_size(bool expand)
{
    maximized = expand;
    lv_obj_set_width(ui_Group_Map, expand ? lv_pct(100) : COMPACT_WIDTH);
    lv_obj_align(status, expand ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT,
                 expand ? 2 : -5, -2);
    lv_label_set_text(size_caption, expand ? ">" : "<");
    lv_obj_update_layout(view);
    update_fix_status();
}

static void toggle_size(lv_event_t *event)
{
    (void)event;
    set_map_size(!maximized);
}

static void open_map(lv_event_t *event)
{
    (void)event;
    if (maximized) set_map_size(false);
    else update_fix_status();
}

static lv_obj_t *map_button(const char *text, int y, lv_event_cb_t action)
{
    lv_obj_t *button = lv_btn_create(view);
    lv_obj_set_size(button, 30, CONTROL_HEIGHT);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -5, y);
    lv_obj_set_style_opa(button, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x304751), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_t *caption = lv_label_create(button);
    lv_label_set_text(caption, text);
    lv_obj_set_style_text_font(caption, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(caption, lv_color_white(), 0);
    lv_obj_center(caption);
    lv_obj_add_event_cb(button, action, LV_EVENT_CLICKED, NULL);
    return button;
}

void map_view_init(void)
{
    lv_obj_add_flag(ui_Map_Bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Marker_Group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Next_Group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Distance_Group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(ui_Group_Map, 0, 0);
    lv_obj_move_foreground(ui_Group_Map);
    view = mapengine_create(ui_Group_Map);
    lv_obj_set_style_text_font(view, &ui_font_Small, 0);
    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    lv_obj_center(view);
    lv_obj_add_event_cb(view, compact_edge, LV_EVENT_COVER_CHECK, NULL);
    lv_obj_add_event_cb(view, compact_edge, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_add_event_cb(view, compact_edge, LV_EVENT_DRAW_MAIN_END, NULL);
    marker = lv_obj_create(view);
    lv_obj_remove_style_all(marker);
    lv_obj_set_size(marker, 11, 11);
    lv_obj_center(marker);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(0x2bbdf4), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(marker, lv_color_white(), 0);
    lv_obj_set_style_border_width(marker, 2, 0);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(marker, draw_vehicle, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(view, drag_preview, LV_EVENT_PRESSING, NULL);
    lv_obj_t *size_button = map_button("<", 5, toggle_size);
    size_caption = lv_obj_get_child(size_button, 0);
    zoom_in = map_button("+", 36, zoom);
    zoom_out = map_button("-", 67, zoom);
    heading_button = map_button("^", 98, toggle_heading);
    lv_obj_set_style_bg_color(heading_button, lv_color_hex(0x08769c), LV_STATE_CHECKED);
    road_button = map_button("*", 129, toggle_road_view);
    lv_obj_set_style_bg_color(road_button, lv_color_hex(0x08769c), LV_STATE_CHECKED);
    lv_obj_add_event_cb(ui_BTN_BG3, open_map, LV_EVENT_CLICKED, NULL);
    status = lv_label_create(view);
    lv_obj_set_style_bg_opa(status, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(status, lv_color_hex(0x14212a), 0);
    lv_obj_set_style_text_color(status, lv_color_white(), 0);
    lv_obj_set_style_text_font(status, &ui_font_Small, 0);
    lv_label_set_text(status, "Beijing preview  |  No GPS fix");
    scale_label = lv_label_create(view);
    lv_obj_set_width(scale_label, MAPENGINE_SCALE_PIXELS);
    lv_obj_align(scale_label, LV_ALIGN_TOP_RIGHT, -42, 7);
    lv_obj_set_style_text_align(scale_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(scale_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(scale_label, lv_color_hex(0x14212a), 0);
    lv_obj_set_style_bg_opa(scale_label, LV_OPA_80, 0);
    lv_obj_set_style_border_side(scale_label, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(scale_label, 1, 0);
    lv_obj_set_style_border_color(scale_label, lv_color_white(), 0);
    lv_obj_set_style_pad_bottom(scale_label, 1, 0);
    sync_zoom_controls();
    set_map_size(false);
    mapengine_set_center(view, (mapengine_point_t){.latitude = 39.907, .longitude = 116.397});
}

void map_view_update(const bike_readings *r)
{
    if (r->fix) {
        if (r->course_valid) {
            last_course = r->course_deg;
            if (mapengine_is_road_view(view) || lv_obj_has_state(heading_button, LV_STATE_CHECKED))
                mapengine_set_heading(view, last_course);
        }
        mapengine_point_t position = mapengine_get_center(view);
        if (!fixed || position.latitude != r->latitude || position.longitude != r->longitude)
            mapengine_set_center(view, (mapengine_point_t){r->latitude,r->longitude});
        if (!fixed) lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        fixed = true;
        last_fix = true;
    } else if (fixed) {
        fixed = false;
        preview_moved = false;
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_is_visible(view)) update_fix_status();
    else {
        mapengine_release(view);
        map_available = false;
    }
}
