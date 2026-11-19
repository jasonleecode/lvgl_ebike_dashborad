#ifndef EBIKE_ROAD_VIEW_H
#define EBIKE_ROAD_VIEW_H
#include "map_layers.h"

/* Pitched ground-plane camera. Ground coordinates are metres right/forward
 * from the location fix. zoom changes the camera distance, not road geometry. */
struct road_camera {
    struct map_projection map;
    double east_unit, focal, back, horizon, vertical, far;
};
struct road_camera road_camera_for(const struct map_projection *map, const lv_area_t *box);
struct map_bounds road_visible_bounds(const struct road_camera *camera, const lv_area_t *clip);
void road_view_draw(struct map_store *store, lv_draw_ctx_t *ctx, const lv_area_t *box,
                    const struct road_camera *camera, int zoom);
#endif
