/* Host-side conversion keeps the IMG parser and its working set off the MCU. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "export_features.h"
#include "pack.h"

enum { TILE = 512 };
struct segment {
    int tx, ty;
    int16_t x1, y1, x2, y2, kind;
};
static struct segment *segments;
static size_t used, capacity;

static void add_segment(int x1, int y1, int x2, int y2, int kind)
{
    if (x1 == x2 && y1 == y2) return;
    /* Keep endpoints within one tile of the midpoint for viewport queries. */
    int mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
    if (abs(x1 - x2) > TILE || abs(y1 - y2) > TILE) {
        add_segment(x1, y1, mx, my, kind);
        add_segment(mx, my, x2, y2, kind);
        return;
    }
    if (used == capacity) {
        capacity = capacity ? capacity * 2 : 65536;
        void *next = realloc(segments, capacity * sizeof(*segments));
        if (!next) { perror("map segments"); exit(1); }
        segments = next;
    }
    int tx = mx / MAP_TILE_UNITS * MAP_TILE_UNITS, ty = my / MAP_TILE_UNITS * MAP_TILE_UNITS;
    segments[used++] = (struct segment){tx, ty, x1-tx, y1-ty, x2-tx, y2-ty, kind};
}

/* Three distinct neighbouring vertices indicate a branch. Ignore duplicate
 * edges and do not create a junction just because two lines cross in plan. */
struct endpoint { int x, y, other_x, other_y; size_t end; };

static int endpoint_order(const void *a, const void *b)
{
    const struct endpoint *p = a, *q = b;
    if (p->x != q->x) return p->x - q->x;
    if (p->y != q->y) return p->y - q->y;
    if (p->other_x != q->other_x) return p->other_x - q->other_x;
    return p->other_y - q->other_y;
}

static void mark_junctions(void)
{
    if (!used) return;
    struct endpoint *ends = malloc(2 * used * sizeof(*ends));
    if (!ends) { perror("road junctions"); exit(1); }
    size_t n = 0;
    for (size_t i = 0; i < used; ++i) {
        const struct segment *s = &segments[i];
        if (!map_is_road(s->kind & MAP_LINE_TYPE)) continue;
        int x1=s->tx+s->x1, y1=s->ty+s->y1, x2=s->tx+s->x2, y2=s->ty+s->y2;
        ends[n++] = (struct endpoint){x1,y1,x2,y2,2*i};
        ends[n++] = (struct endpoint){x2,y2,x1,y1,2*i+1};
    }
    qsort(ends, n, sizeof(*ends), endpoint_order);
    for (size_t i=0; i<n;) {
        size_t j=i+1, neighbours=1;
        while (j<n && ends[j].x==ends[i].x && ends[j].y==ends[i].y) {
            if (ends[j].other_x!=ends[j-1].other_x || ends[j].other_y!=ends[j-1].other_y) ++neighbours;
            ++j;
        }
        if (neighbours >= 3)
            for (size_t k=i; k<j; ++k)
                segments[ends[k].end/2].kind |= ends[k].end%2 ? MAP_LINE_JUNCTION_END : MAP_LINE_JUNCTION_START;
        i=j;
    }
    free(ends);
}

static int tile_order(const void *a, const void *b)
{
    const struct segment *s = a, *t = b;
    if (s->ty != t->ty || s->tx != t->tx)
        return pack_tile_order(s->tx,s->ty) < pack_tile_order(t->tx,t->ty) ? -1 : 1;
    return (t->kind & MAP_LINE_TYPE) - (s->kind & MAP_LINE_TYPE); /* Draw the major roads last. */
}

static void log_message(char *file, int line, int level, char *fmt, ...)
{
    (void)file; (void)line; (void)level; (void)fmt;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    struct gar_config cfg = {.opm = OPM_GPS};
    struct gar *gar = gar_init_cfg(NULL, log_message, &cfg);
    if (!gar || gar_img_load(gar, argv[1], 1) <= 0) {
        fprintf(stderr, "Cannot load Garmin IMG: %s\n", argv[1]);
        return 1;
    }
    struct gmap *map = gar_find_subfiles(gar, NULL, 0);
    struct gobject *objects = NULL;
    if (!map || gar_get_objects(map, map->basebits + map->zoomlevels, NULL, &objects, 0) <= 0)
        return 1;
    for (struct gobject *o = objects; o; o = o->next) {
        struct gcoord p, d;
        if (o->type != GO_POLYLINE) {
            collect_features(map, o);
            continue;
        }
        if (!gar_get_object_coord(map, o, &p)) continue;
        int kind = gar_obj_type(o);
        if (map_is_road(kind) && (gar_object_flags(o) & F_ONEWAY)) kind |= MAP_LINE_ONEWAY;
        int count = gar_get_object_deltas(o);
        for (int n = 0; n+1 < count && gar_get_object_dcoord(map, o, n, &d); ++n) {
            add_segment(p.x, p.y, p.x+d.x, p.y+d.y, kind);
            p.x += d.x;
            p.y += d.y;
        }
    }
    mark_junctions();
    qsort(segments, used, sizeof(*segments), tile_order);
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }
    pack_begin(out);
    for (size_t i=0; i<used; ++i) {
        const struct segment *s = &segments[i];
        struct map_line line = {s->x1,s->y1,s->x2,s->y2,s->kind};
        struct map_bounds b = {s->tx + (s->x1<s->x2?s->x1:s->x2), s->ty + (s->y1<s->y2?s->y1:s->y2),
                               s->tx + (s->x1>s->x2?s->x1:s->x2), s->ty + (s->y1>s->y2?s->y1:s->y2)};
        pack_record(MAP_LINE_TREE,s->tx,s->ty,b,-2,&line,sizeof(line),NULL,0);
    }
    if (!write_features() && !used) { fprintf(stderr,"Empty map\n"); return 1; }
    pack_finish();
    fprintf(stderr, "Garmin lines: %zu segments\n", used);
    return fclose(out) != 0;
}
