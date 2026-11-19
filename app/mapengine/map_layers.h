#ifndef EBIKE_MAP_LAYERS_H
#define EBIKE_MAP_LAYERS_H

#include "map_store.h"
#include "lvgl/lvgl.h"

#define MAP_METRES_PER_UNIT (40030228.9 / 16777216.0)

struct map_projection {
    double x, y, px, py;
    int cx, cy;
    double cos_heading, sin_heading;
};

struct screen_point { double x, y; };

/* Compass headings are clockwise from north. Rotate east/north into right/down. */
static inline struct screen_point map_project(const struct map_projection *p, double x, double y)
{
    double east = (x - p->x) * p->px, north = (y - p->y) * p->py;
    return (struct screen_point){p->cx + east*p->cos_heading - north*p->sin_heading,
                                 p->cy - east*p->sin_heading - north*p->cos_heading};
}
struct place_symbol {
    lv_area_t mark, label;
    uint32_t name_hash;
    unsigned kind;
    char caption[48];
};
struct place_layout {
    struct place_symbol symbols[48];
    unsigned count;
    bool valid;
    lv_area_t box;
    const lv_font_t *font;
    lv_obj_t *view;
};

uint32_t map_area_color(unsigned kind);
struct map_bounds map_visible_bounds(const struct map_projection *p, const lv_area_t *clip);
void map_draw_areas(struct map_store *store, lv_draw_ctx_t *ctx, const struct map_projection *p, int zoom);
void map_draw_places(struct map_store *store, lv_draw_ctx_t *ctx, const struct map_projection *p, const lv_area_t *box,
                     int zoom, lv_obj_t *view, struct place_layout *layout);
#endif
