#include "mapengine.h"
#include "map_layers.h"
#include "map_platform.h"
#include "road_view.h"
#include <math.h>

static const double units_per_degree = 16777216.0 / 360;

typedef struct {
    lv_obj_t obj;
    double x, y;          /* Center in signed Garmin coordinate units. */
    double latitude_cos;
    int zoom, last_overview_zoom;
    double heading, cos_heading, sin_heading;
    struct place_layout places;
    struct map_store store;
    uint32_t last_attempt;
    bool attempted;
} map_widget_t;

void mapengine_release(lv_obj_t *obj)
{
    map_widget_t *m = (map_widget_t *)obj;
    map_store_close(&m->store);
    m->places.valid = false;
    m->attempted = false;
}

static void destroy_map(const lv_obj_class_t *cls,lv_obj_t *obj)
{
    (void)cls;
    mapengine_release(obj);
}

bool mapengine_prepare(lv_obj_t *obj)
{
    map_widget_t *m = (map_widget_t *)obj;
    if (m->store.ready) return true;
    if (m->attempted && lv_tick_elaps(m->last_attempt)<3000) return false;
    m->attempted = true;
    m->last_attempt = lv_tick_get();
    m->places.valid = false;
    return map_store_open(&m->store,platform_map_path());
}

static const lv_obj_class_t map_class = {
    .destructor_cb = destroy_map,
    .base_class = &lv_obj_class,
    .instance_size = sizeof(map_widget_t)
};

double mapengine_get_scale_metres(const lv_obj_t *obj)
{
    return ldexp(10.0, MAPENGINE_10M_ZOOM - mapengine_get_zoom(obj));
}

bool mapengine_is_road_view(const lv_obj_t *obj)
{
    return mapengine_get_zoom(obj) > MAPENGINE_10M_ZOOM;
}

static double units_per_pixel(const map_widget_t *map)
{
    return mapengine_get_scale_metres(&map->obj) / (MAPENGINE_SCALE_PIXELS * MAP_METRES_PER_UNIT);
}

void mapengine_set_center(lv_obj_t *obj, mapengine_point_t position)
{
    map_widget_t *map = (map_widget_t *)obj;
    if (!isfinite(position.latitude) || !isfinite(position.longitude)) return;
    double lat = fmax(-90, fmin(90, position.latitude));
    double lon = fmax(-180, fmin(180, position.longitude));
    double x = lon * units_per_degree, y = lat * units_per_degree;
    if (x == map->x && y == map->y) return;
    map->x = x;
    map->y = y;
    map->latitude_cos = fmax(0.01, cos(lat * 0.01745329252));
    map->places.valid = false;
    lv_obj_invalidate(obj);
}

mapengine_point_t mapengine_get_center(const lv_obj_t *obj)
{
    const map_widget_t *map = (const map_widget_t *)obj;
    return (mapengine_point_t){map->y / units_per_degree, map->x / units_per_degree};
}

static struct map_projection projection_for(const map_widget_t *map, const lv_area_t *box)
{
    double scale = units_per_pixel(map);
    return (struct map_projection){map->x, map->y, map->latitude_cos/scale, 1.0/scale,
        box->x1 + lv_area_get_width(box)/2, box->y1 + lv_area_get_height(box)/2,
        map->cos_heading, map->sin_heading};
}

void mapengine_pan(lv_obj_t *obj, lv_coord_t dx, lv_coord_t dy)
{
    const map_widget_t *map = (const map_widget_t *)obj;
    double degrees_per_pixel = units_per_pixel(map) / units_per_degree;
    mapengine_point_t position = mapengine_get_center(obj);
    double right = dx*degrees_per_pixel, down = dy*degrees_per_pixel;
    if (mapengine_is_road_view(obj)) {
        /* Drag at the vehicle anchor, scaled by the local perspective. */
        lv_area_t box;
        lv_obj_get_coords(obj, &box);
        if (lv_area_get_height(&box) <= 0) return;
        struct map_projection projection = projection_for(map, &box);
        struct road_camera camera = road_camera_for(&projection, &box);
        const double metres_per_degree = 40030228.9 / 360.0;
        right = dx * camera.back / (camera.focal * metres_per_degree);
        down = dy * camera.back * camera.back / (camera.vertical * metres_per_degree);
    }
    position.longitude -= (map->cos_heading*right - map->sin_heading*down) / map->latitude_cos;
    position.latitude += map->sin_heading*right + map->cos_heading*down;
    mapengine_set_center(obj, position);
}

void mapengine_set_heading(lv_obj_t *obj, double degrees)
{
    map_widget_t *map = (map_widget_t *)obj;
    if (!isfinite(degrees)) return;
    degrees = remainder(degrees, 360.0);
    if (degrees == map->heading) return;
    map->heading = degrees;
    double radians = degrees * 0.017453292519943295;
    map->cos_heading = cos(radians);
    map->sin_heading = sin(radians);
    map->places.valid = false;
    lv_obj_invalidate(obj);
}

void mapengine_set_road_view(lv_obj_t *obj, bool enabled)
{
    map_widget_t *map = (map_widget_t *)obj;
    if (mapengine_is_road_view(obj) == enabled) return;
    mapengine_set_zoom(obj, enabled ? MAPENGINE_10M_ZOOM + 1 : map->last_overview_zoom);
}

int mapengine_get_zoom(const lv_obj_t *obj)
{
    return ((const map_widget_t *)obj)->zoom;
}

void mapengine_set_zoom(lv_obj_t *obj, int level)
{
    map_widget_t *map = (map_widget_t *)obj;
    level = LV_CLAMP(MAPENGINE_MIN_ZOOM, level, MAPENGINE_MAX_ZOOM);
    if (level == map->zoom) return;
    if (!mapengine_is_road_view(obj) && level > MAPENGINE_10M_ZOOM)
        map->last_overview_zoom = map->zoom;
    map->zoom = level;
    map->places.valid = false;
    lv_obj_invalidate(obj);
}

bool mapengine_has_coverage(const lv_obj_t *obj)
{
    map_widget_t *map = (map_widget_t *)obj;
    if (!mapengine_prepare((lv_obj_t *)obj)) return false;
    lv_area_t box = {0,0,lv_obj_get_width(obj)-1,lv_obj_get_height(obj)-1};
    const struct map_projection p = projection_for(map, &box);
    struct map_bounds b;
    if (mapengine_is_road_view(obj)) {
        struct road_camera camera = road_camera_for(&p, &box);
        b = road_visible_bounds(&camera, &box);
    } else {
        lv_area_increase(&box,3,3);
        b = map_visible_bounds(&p,&box);
    }
    map_store_select(&map->store,b);
    return map_store_visit(&map->store,MAP_LINE,b,map->zoom,NULL,NULL) ||
           map_store_visit(&map->store,MAP_AREA,b,map->zoom,NULL,NULL) ||
           map_store_visit(&map->store,MAP_PLACE,b,map->zoom,NULL,NULL);
}

static uint32_t line_color(int kind)
{
    if (kind <= 5) return 0xe5b677;
    if (kind < 0x14) return 0x8294a3;
    if (kind == 0x14) return 0x888899;                        /* railway */
    if (kind == 0x16) return 0x497765;                        /* trail */
    if (kind == 0x15 || kind == 0x18 || kind == 0x1f || kind == 0x26) return 0x4e98b9;
    if (kind == 0x1a || kind == 0x1b) return 0x76a4ae;        /* ferry */
    if (kind >= 0x1c && kind <= 0x1e) return 0x856a83;        /* boundary */
    if (kind >= 0x20 && kind <= 0x25) return 0x5a554d;        /* contour */
    if (kind == 0x27) return 0x788493;                        /* runway */
    return 0x4b5360;                                         /* utilities and other lines */
}

struct line_query {
    lv_draw_ctx_t *ctx;
    const struct map_projection *p;
    double scale;
};

static bool draw_line_page(const struct map_page *page, void *user)
{
    const struct line_query *q=user;
    const lv_area_t *clip=q->ctx->clip_area;
    const double scale = q->scale;
    const int margin=3;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    const struct map_line *s=(const struct map_line *)(page+1);
    for (unsigned i=0;i<page->count;++i,++s) {
        struct screen_point start = map_project(q->p, page->x+s->x1, page->y+s->y1);
        struct screen_point end = map_project(q->p, page->x+s->x2, page->y+s->y2);
        lv_point_t a = {start.x, start.y}, b = {end.x, end.y};
        if ((a.x < clip->x1-margin && b.x < clip->x1-margin) || (a.y < clip->y1-margin && b.y < clip->y1-margin) ||
            (a.x > clip->x2+margin && b.x > clip->x2+margin) || (a.y > clip->y2+margin && b.y > clip->y2+margin)) continue;
        int kind = s->kind & MAP_LINE_TYPE;
        line.width = kind <= 5 ? 2 : 1;
        if (scale < 1 && kind <= 0x0d)
            line.width = kind <= 5 ? 4 : 2;
        line.round_start = line.round_end = line.width > 1;
        line.color = lv_color_hex(line_color(kind));
        lv_draw_line(q->ctx, &line, &a, &b);
    }
    return false;
}

static void draw_map(lv_event_t *event)
{
    lv_obj_t *view = lv_event_get_target(event);
    map_widget_t *map = (map_widget_t *)view;
    if (!mapengine_prepare(view)) return;
    double scale = units_per_pixel(map);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_area_t box;
    lv_obj_get_coords(view, &box);
    lv_area_t clip;
    if (!_lv_area_intersect(&clip, &box, ctx->clip_area)) return;
    const lv_area_t *old_clip = ctx->clip_area;
    ctx->clip_area = &clip;
    const struct map_projection projection = projection_for(map, &box);
    if (mapengine_is_road_view(view)) {
        struct road_camera camera = road_camera_for(&projection, &box);
        road_view_draw(&map->store, ctx, &box, &camera, map->zoom);
    } else {
        lv_area_t viewport = box;
        lv_area_increase(&viewport,3,3);
        map_store_select(&map->store,map_visible_bounds(&projection,&viewport));
        map_draw_areas(&map->store,ctx,&projection,map->zoom);
        lv_area_t strokes = clip;
        lv_area_increase(&strokes,3,3);
        struct line_query q = {ctx,&projection,scale};
        map_store_visit(&map->store,MAP_LINE,map_visible_bounds(&projection,&strokes),map->zoom,draw_line_page,&q);
        map_draw_places(&map->store,ctx, &projection, &box, map->zoom, view, &map->places);
    }
    if (!map->store.ready) map->last_attempt = lv_tick_get();
    ctx->clip_area = old_clip;
}

lv_obj_t *mapengine_create(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_class_create_obj(&map_class, parent);
    if (!obj) return NULL;
    lv_obj_class_init_obj(obj);
    map_widget_t *map = (map_widget_t *)obj;
    map->latitude_cos = 1;
    map->cos_heading = 1;
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x14212a), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, draw_map, LV_EVENT_DRAW_MAIN, NULL);
    return obj;
}
