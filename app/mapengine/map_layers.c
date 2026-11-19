#include "map_layers.h"
#include <math.h>
#include <string.h>

struct map_bounds map_visible_bounds(const struct map_projection *p, const lv_area_t *clip)
{
    double dx = (clip->x1 + clip->x2)*0.5 - p->cx, dy = (clip->y1 + clip->y2)*0.5 - p->cy;
    double hw = (clip->x2 - clip->x1)*0.5 + 1, hh = (clip->y2 - clip->y1)*0.5 + 1;
    double c = p->cos_heading, s = p->sin_heading;
    double x = p->x + (c*dx - s*dy)/p->px, y = p->y - (s*dx + c*dy)/p->py;
    double rx = (fabs(c)*hw + fabs(s)*hh)/p->px, ry = (fabs(s)*hw + fabs(c)*hh)/p->py;
    return (struct map_bounds){floor(x-rx), floor(y-ry), ceil(x+rx), ceil(y+ry)};
}

static bool contains(struct map_bounds b, int x, int y)
{
    return x >= b.west && x <= b.east && y >= b.south && y <= b.north;
}

uint32_t map_area_color(unsigned kind)
{
    if (kind == 0x51) return 0x284b47; /* wetlands */
    if ((kind >= 0x28 && kind <= 0x49) || kind == 0x4c) return 0x214963;
    if ((kind >= 0x14 && kind <= 0x20) || (kind >= 0x4e && kind <= 0x50))
        return kind == 0x50 ? 0x233e37 : 0x294b3d;
    switch (kind) {
    case 0x05: case 0x06: return 0x37434c; /* parking */
    case 0x0a: return 0x363944;            /* campus */
    case 0x0b: return 0x49383f;            /* hospital */
    case 0x08: return 0x403c40;            /* retail */
    case 0x13: return 0x35424a;            /* man-made footprint */
    default: return 0x25323a;
    }
}

/* LVGL 8's polygon primitive only supports convex rings. Even-odd scanlines
 * also fill concave shorelines and rings with holes joined by a return edge. */
static void fill_area(lv_draw_ctx_t *ctx, const struct map_projection *p, const struct map_area *a)
{
    const lv_area_t *clip = ctx->clip_area;
    double width = (a->bounds.east-a->bounds.west)*p->px;
    double height = (a->bounds.north-a->bounds.south)*p->py;
    double c = fabs(p->cos_heading), s = fabs(p->sin_heading);
    if (c*width + s*height < 0.8 || s*width + c*height < 0.8) return;
    float *vx = lv_mem_buf_get(3 * a->count * sizeof(*vx));
    if (!vx) return;
    float *vy = vx + a->count, *crossings = vy + a->count;
    const uint16_t *offsets = (const uint16_t *)(a+1);
    float top = INFINITY, bottom = -INFINITY;
    for (unsigned i=0; i<a->count; ++i) {
        struct screen_point point = map_project(p, a->bounds.west + offsets[2*i],
                                                a->bounds.south + offsets[2*i+1]);
        vx[i] = point.x;
        vy[i] = point.y;
        if (vy[i] < top) top = vy[i];
        if (vy[i] > bottom) bottom = vy[i];
    }
    int first = LV_MAX(clip->y1, (int)ceilf(top-0.5f));
    int last = LV_MIN(clip->y2, (int)ceilf(bottom-0.5f)-1);
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_hex(map_area_color(a->kind));
    for (int y=first; y<=last; ++y) {
        float scan_y = y+0.5f;
        unsigned n = 0;
        for (unsigned i=0, prev=a->count-1; i<a->count; prev=i++) {
            if ((vy[i] > scan_y) == (vy[prev] > scan_y)) continue;
            float x = vx[i] + (scan_y-vy[i])*(vx[prev]-vx[i])/(vy[prev]-vy[i]);
            unsigned k = n++;
            while (k && crossings[k-1] > x) { crossings[k] = crossings[k-1]; --k; }
            crossings[k] = x;
        }
        for (unsigned i=1; i<n; i+=2) {
            int x1 = LV_MAX(clip->x1, (int)ceilf(crossings[i-1]-0.5f));
            int x2 = LV_MIN(clip->x2, (int)ceilf(crossings[i]-0.5f)-1);
            if (x1 <= x2) {
                lv_area_t span = {x1,y,x2,y};
                lv_draw_rect(ctx, &fill, &span);
            }
        }
    }
    lv_mem_buf_release(vx);
}

struct area_query {
    lv_draw_ctx_t *ctx;
    const struct map_projection *projection;
    struct map_bounds bounds;
};

static bool draw_area_page(const struct map_page *page, void *user)
{
    const struct area_query *q = user;
    const struct map_area *area = (const struct map_area *)(page+1);
    for (unsigned i=0; i<page->count; ++i) {
        if (map_intersects(q->bounds, area->bounds)) fill_area(q->ctx,q->projection,area);
        area = (const struct map_area *)((const uint16_t *)(area+1) + 2*area->count);
    }
    return false;
}

void map_draw_areas(struct map_store *store, lv_draw_ctx_t *ctx, const struct map_projection *p, int zoom)
{
    struct area_query q = {ctx,p,map_visible_bounds(p,ctx->clip_area)};
    map_store_visit(store,MAP_AREA,q.bounds,zoom,draw_area_page,&q);
}

static bool occupied(const struct place_layout *layout, lv_area_t rect, int padding)
{
    lv_area_increase(&rect, padding, padding);
    /* Keep labels clear of overlays such as the position marker and zoom buttons. */
    for (uint32_t i=0; i<lv_obj_get_child_cnt(layout->view); ++i) {
        lv_obj_t *child = lv_obj_get_child(layout->view, i);
        lv_area_t child_box;
        lv_obj_get_coords(child, &child_box);
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) && _lv_area_is_on(&rect, &child_box)) return true;
    }
    for (unsigned i=0; i<layout->count; ++i) {
        const struct place_symbol *s = &layout->symbols[i];
        if (_lv_area_is_on(&rect, &s->mark) || (s->caption[0] && _lv_area_is_on(&rect, &s->label)))
            return true;
    }
    return false;
}

static void add_symbol(struct place_layout *layout, const struct map_place *place,
                       int x, int y, int zoom)
{
    lv_area_t mark = {x-3,y-3,x+3,y+3};
    if (!_lv_area_is_in(&mark, &layout->box, 0) || occupied(layout, mark, zoom < 1 ? 9 : 5)) return;
    struct place_symbol *s = &layout->symbols[layout->count];
    s->kind = place->kind;
    const char *name = (const char *)(place+1);
    s->name_hash = map_checksum(name,strlen(name));
    s->mark = mark;
    s->caption[0] = 0;
    bool show_name = *name != 0;
    for (unsigned i=0; show_name && i<layout->count; ++i) {
        const struct place_symbol *other = &layout->symbols[i];
        if (other->caption[0] && other->name_hash == s->name_hash)
            show_name = false;
    }
    if (show_name) {
        size_t len = strlen(name);
        size_t bytes = LV_MIN(len, sizeof(s->caption)-4);
        while (bytes && ((unsigned char)name[bytes] & 0xc0) == 0x80) --bytes;
        memcpy(s->caption, name, bytes);
        s->caption[bytes] = 0;
        int max_width = LV_MIN(150, lv_area_get_width(&layout->box)/2);
        while (bytes && lv_txt_get_width(s->caption, strlen(s->caption), layout->font, 0, LV_TEXT_FLAG_NONE) > max_width-10) {
            do { --bytes; } while (bytes && ((unsigned char)name[bytes] & 0xc0) == 0x80);
            s->caption[bytes] = 0;
        }
        if (bytes < len) strcpy(s->caption+bytes, "...");
        int width = lv_txt_get_width(s->caption, strlen(s->caption), layout->font, 0, LV_TEXT_FLAG_NONE);
        int left = x+6;
        int top = y-layout->font->line_height/2;
        s->label = (lv_area_t){left-1,top,left+width+1,top+layout->font->line_height-1};
        if (!_lv_area_is_in(&s->label, &layout->box, 0) || occupied(layout, s->label, 3)) {
            lv_area_move(&s->label, -(width+12), 0);
            if (!_lv_area_is_in(&s->label, &layout->box, 0) || occupied(layout, s->label, 3))
                s->caption[0] = 0;
        }
    }
    ++layout->count;
}

struct place_query {
    struct place_layout *layout;
    const struct map_projection *p;
    struct map_bounds bounds;
    int zoom;
};

static bool layout_page(const struct map_page *page, void *user)
{
    const struct place_query *q = user;
    const struct map_projection *p = q->p;
    const struct map_place *place = (const struct map_place *)(page+1);
    for (unsigned i=0;i<page->count;++i) {
        int x = page->x+place->x, y = page->y+place->y;
        if (place->min_zoom<=q->zoom && contains(q->bounds,x,y)) {
            struct screen_point point = map_project(p, x, y);
            add_symbol(q->layout, place, point.x, point.y, q->zoom);
            if (q->layout->count == sizeof(q->layout->symbols)/sizeof(q->layout->symbols[0])) return true;
        }
        place=(const struct map_place *)((const char *)(place+1)+place->text_bytes);
    }
    return false;
}

void map_draw_places(struct map_store *store, lv_draw_ctx_t *ctx, const struct map_projection *p, const lv_area_t *box,
                     int zoom, lv_obj_t *view, struct place_layout *layout)
{
    const lv_font_t *font = lv_obj_get_style_text_font(view, LV_PART_MAIN);
    layout->view = view;
    if (!layout->valid || !_lv_area_is_equal(box, &layout->box) || font != layout->font) {
        layout->count = 0;
        layout->box = *box;
        layout->font = font;
        struct place_query q = {layout,p,map_visible_bounds(p,box),zoom};
        map_store_visit(store,MAP_PLACE,q.bounds,zoom,layout_page,&q);
        layout->valid = store->ready;
    }
    if (!store->ready) return;
    static const uint32_t colors[] = {
        [PLACE_SETTLEMENT]=0xe5dfc9, [PLACE_PARK]=0x81bd89, [PLACE_WATER]=0x75b9d7,
        [PLACE_TRANSIT]=0x8cb6eb, [PLACE_MEDICAL]=0xe79191, [PLACE_LANDMARK]=0xd9b779,
        [PLACE_PARKING]=0x8cb6eb, [PLACE_SERVICE]=0xc9a0b3, [PLACE_OTHER]=0xa1b4bb
    };
    lv_draw_rect_dsc_t dot, backing;
    lv_draw_rect_dsc_init(&dot);
    dot.radius = LV_RADIUS_CIRCLE;
    dot.border_width = 1;
    dot.border_color = lv_color_hex(0x15242a);
    lv_draw_rect_dsc_init(&backing);
    backing.bg_color = dot.border_color;
    backing.bg_opa = LV_OPA_80;
    lv_draw_label_dsc_t text;
    lv_draw_label_dsc_init(&text);
    text.font = font;
    text.flag = LV_TEXT_FLAG_EXPAND;
    text.color = lv_color_hex(0xe1e7e6);
    for (unsigned i=0; i<layout->count; ++i) {
        const struct place_symbol *s = &layout->symbols[i];
        if (_lv_area_is_on(&s->mark, ctx->clip_area)) {
            dot.bg_color = lv_color_hex(colors[s->kind]);
            lv_draw_rect(ctx, &dot, &s->mark);
        }
        if (s->caption[0] && _lv_area_is_on(&s->label, ctx->clip_area)) {
            lv_draw_rect(ctx, &backing, &s->label);
            lv_area_t label = s->label;
            ++label.x1;
            lv_draw_label(ctx, &text, &label, s->caption, NULL);
        }
    }
}
