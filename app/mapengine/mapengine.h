#ifndef EBIKE_MAPENGINE_H
#define EBIKE_MAPENGINE_H

#include "lvgl/lvgl.h"

typedef struct {
    double latitude;
    double longitude;
} mapengine_point_t;

/* The scale bar measures horizontal ground distance at the vehicle position. */
enum { MAPENGINE_SCALE_PIXELS = 40, MAPENGINE_MIN_ZOOM = -2, MAPENGINE_10M_ZOOM = 4,
       MAPENGINE_MAX_ZOOM = MAPENGINE_10M_ZOOM + 2 };

/* An independently pannable LVGL widget. The parent owns its lifetime. */
lv_obj_t *mapengine_create(lv_obj_t *parent);
void mapengine_set_center(lv_obj_t *map, mapengine_point_t position);
mapengine_point_t mapengine_get_center(const lv_obj_t *map);
/* Translate the map contents by a screen-space drag in pixels. */
void mapengine_pan(lv_obj_t *map, lv_coord_t dx, lv_coord_t dy);
/* True course in degrees clockwise from north, with zero for north-up. */
void mapengine_set_heading(lv_obj_t *map, double degrees);
double mapengine_get_scale_metres(const lv_obj_t *map);
bool mapengine_is_road_view(const lv_obj_t *map);
/* Shortcut between the first forward-view scale and the last overview scale. */
void mapengine_set_road_view(lv_obj_t *map, bool enabled);
void mapengine_set_zoom(lv_obj_t *map, int level);
int mapengine_get_zoom(const lv_obj_t *map);
bool mapengine_prepare(lv_obj_t *map);
void mapengine_release(lv_obj_t *map);
bool mapengine_has_coverage(const lv_obj_t *map);

#endif
