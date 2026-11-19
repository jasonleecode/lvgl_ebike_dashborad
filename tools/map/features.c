#include "export_features.h"
#include "pack.h"
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

struct area_feature { struct map_bounds bounds; uint32_t first; uint16_t count, kind; };
struct place_feature { int32_t x,y; unsigned name; uint16_t kind; int16_t min_zoom; };
struct indexed_feature {
    struct map_bounds bounds;
    unsigned index;
    int layer;
    uint64_t key;
};
static struct area_feature *areas;
static struct place_feature *places;
static struct gcoord *vertices;
static char *names;
static size_t area_count, place_count, vertex_count, names_size;
static size_t area_cap, place_cap, vertex_cap, names_cap;

static void *reserve(void *data, size_t *cap, size_t need, size_t size)
{
    if (need <= *cap) return data;
    *cap = (*cap + need) * 2;
    data = realloc(data, *cap * size);
    if (!data) { perror("map features"); exit(1); }
    return data;
}

static void include(struct map_bounds *b, int x, int y)
{
    if (x < b->west) b->west = x;
    if (x > b->east) b->east = x;
    if (y < b->south) b->south = y;
    if (y > b->north) b->north = y;
}

/* The Beijing IMG uses Windows-1252 for accented Pinyin. Decode on the host. */
static unsigned store_name(struct gobject *o)
{
    static iconv_t decoder = (iconv_t)-1;
    static char encoding[80];
    char *raw = gar_get_object_lbl(o);
    if (!raw) return 0;
    const char *cp = gar_obj_codepage(o);
    if (!cp) cp = "ASCII";
    if (strcmp(cp, encoding)) {
        if (decoder != (iconv_t)-1) iconv_close(decoder);
        decoder = iconv_open("UTF-8", cp);
        snprintf(encoding, sizeof encoding, "%s", cp);
    }
    char *dst = raw;
    for (const unsigned char *src = (unsigned char *)raw; *src; ++src)
        if (*src >= 32 && *src != 127) *dst++ = *src;
    *dst = 0;
    char *alias = strstr(raw, ": ");
    if (alias && (size_t)(alias-raw) == strlen(alias+2) && !strncmp(raw, alias+2, alias-raw))
        *alias = 0;
    size_t len = strlen(raw);
    unsigned offset = 0;
    if (len && decoder != (iconv_t)-1) {
        if (!names_size) names_size = 1; /* Offset zero denotes an unnamed feature. */
        names = reserve(names, &names_cap, names_size + len*4 + 1, 1);
        names[0] = 0;
        char *in = raw, *out = names + names_size;
        size_t left = len*4;
        iconv(decoder, NULL, NULL, NULL, NULL);
        if (iconv(decoder, &in, &len, &out, &left) != (size_t)-1) {
            *out++ = 0;
            offset = names_size;
            names_size = out - names;
        }
    }
    free(raw);
    return offset;
}

static void add_place(int x, int y, int kind, int zoom, unsigned name)
{
    places = reserve(places, &place_cap, place_count+1, sizeof(*places));
    places[place_count++] = (struct place_feature){x, y, name, kind, zoom};
}

static int poi_kind(int type)
{
    int group = type >> 8;
    if (group <= 0x11 || group == 0x1e || group == 0x28) return PLACE_SETTLEMENT;
    if (type == 0x2c06 || group == 0x49) return PLACE_PARK;
    if (group == 0x65 || group == 0x50) return PLACE_WATER;
    if (type == 0x3002 || type == 0x2e05) return PLACE_MEDICAL;
    if (type == 0x2f0b || group == 0x4d) return PLACE_PARKING;
    if (type == 0x2f08 || type == 0x2f17 || group == 0x59) return PLACE_TRANSIT;
    if (group == 0x2c || group == 0x64) return PLACE_LANDMARK;
    if (group >= 0x2a && group <= 0x30) return PLACE_SERVICE;
    return PLACE_OTHER;
}

static int area_layer(int kind)
{
    if (kind <= 3 || kind == 0x10) return 0;
    if (kind == 0x13) return 4; /* Building footprints and other man-made areas. */
    if (kind >= 0x28 && kind <= 0x49) return 3;
    if (kind >= 0x14) return 1;
    return 2;
}

static int compare_x(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Place an area name inside its widest span at mid-height, including concave rings. */
static void add_area_name(struct gobject *o, const struct area_feature *a)
{
    unsigned name = store_name(o);
    if (!name) return;
    static double *crossings;
    static size_t capacity;
    crossings = reserve(crossings, &capacity, a->count, sizeof(*crossings));
    const struct gcoord *v = vertices + a->first;
    double y = (a->bounds.south+a->bounds.north)*0.5;
    unsigned count = 0;
    for (unsigned i=0, prev=a->count-1; i<a->count; prev=i++) {
        if ((v[i].y > y) == (v[prev].y > y)) continue;
        crossings[count++] = v[i].x + (y-v[i].y)*(v[prev].x-v[i].x)/(v[prev].y-v[i].y);
    }
    if (count < 2) return;
    qsort(crossings, count, sizeof(*crossings), compare_x);
    unsigned best = 1;
    for (unsigned i=3; i<count; i+=2)
        if (crossings[i]-crossings[i-1] > crossings[best]-crossings[best-1]) best = i;
    int x = (crossings[best-1]+crossings[best])*0.5;
    int kind = PLACE_OTHER, zoom = 2;
    if (a->kind == 0x17 || a->kind == 0x1e || a->kind == 0x50) { kind = PLACE_PARK; zoom = 0; }
    else if (a->kind >= 0x28 && a->kind <= 0x49) { kind = PLACE_WATER; zoom = 0; }
    else if (a->kind == 0x0b) { kind = PLACE_MEDICAL; zoom = 0; }
    else if (a->kind == 0x0a) { kind = PLACE_LANDMARK; zoom = 1; }
    else if (a->kind == 0x05 || a->kind == 0x06) { kind = PLACE_PARKING; zoom = 1; }
    add_place(x, (int)y, kind, zoom, name);
}

void collect_features(struct gmap *map, struct gobject *o)
{
    struct gcoord p, delta;
    if (!gar_get_object_coord(map, o, &p)) return;
    int type = gar_obj_type(o);
    if (o->type == GO_POINT) {
        int kind = poi_kind(type);
        int zoom = kind == PLACE_SETTLEMENT ? -2 : kind <= PLACE_LANDMARK ? 0 : 1;
        if (type == 0x2f17) zoom = 1; /* Dense urban bus stops. */
        add_place(p.x, p.y, kind, zoom, store_name(o));
        return;
    }
    if (o->type != GO_POLYGON || type == 0x4a || type == 0x4b) return;
    int n = gar_get_object_deltas(o);
    if (n < 3) return;
    if (n > UINT16_MAX) { fprintf(stderr, "Map polygon too large\n"); exit(1); }
    unsigned first = vertex_count;
    vertices = reserve(vertices, &vertex_cap, vertex_count+n, sizeof(*vertices));
    struct map_bounds b = {p.x,p.y,p.x,p.y};
    for (int i = 0; i < n; ++i) {
        vertices[vertex_count++] = p;
        include(&b, p.x, p.y);
        if (i+1 < n && gar_get_object_dcoord(map, o, i, &delta)) {
            p.x += delta.x;
            p.y += delta.y;
        }
    }
    areas = reserve(areas, &area_cap, area_count+1, sizeof(*areas));
    areas[area_count++] = (struct area_feature){b, first, n, type};
    add_area_name(o, &areas[area_count-1]);
}

static int compare_features(const void *a, const void *b)
{
    const struct indexed_feature *s = a, *t = b;
    if (s->layer != t->layer) return s->layer - t->layer;
    if (s->key != t->key) return s->key < t->key ? -1 : 1;
    return (s->index > t->index) - (s->index < t->index);
}

unsigned write_features(void)
{
    struct indexed_feature *area_index = calloc(area_count+1, sizeof(*area_index));
    struct indexed_feature *place_index = calloc(place_count+1, sizeof(*place_index));
    if (!area_index || !place_index) { perror("map index"); exit(1); }
    for (unsigned i=0; i<area_count; ++i)
        area_index[i] = (struct indexed_feature){areas[i].bounds, i, area_layer(areas[i].kind),pack_tile_order(areas[i].bounds.west,areas[i].bounds.south)};
    for (unsigned i=0; i<place_count; ++i) {
        struct place_feature p = places[i];
        place_index[i] = (struct indexed_feature){{p.x,p.y,p.x,p.y},i,p.kind,pack_tile_order(p.x,p.y)};
    }
    qsort(area_index, area_count, sizeof(*area_index), compare_features);
    qsort(place_index, place_count, sizeof(*place_index), compare_features);
    for (size_t i=0; i<area_count; ++i) {
        const struct area_feature *a = &areas[area_index[i].index];
        struct map_area record = {a->bounds,a->count,a->kind};
        uint16_t offsets[MAP_PAGE_MAX/sizeof(uint16_t)];
        if (a->count*4+sizeof(record)+sizeof(struct map_page)>MAP_PAGE_MAX ||
            a->bounds.east-a->bounds.west>UINT16_MAX || a->bounds.north-a->bounds.south>UINT16_MAX) {
            fprintf(stderr,"Area exceeds tile format limits\n"); exit(1);
        }
        for (unsigned j=0;j<a->count;++j) {
            offsets[2*j]=vertices[a->first+j].x-a->bounds.west;
            offsets[2*j+1]=vertices[a->first+j].y-a->bounds.south;
        }
        pack_record(area_layer(a->kind),a->bounds.west/MAP_TILE_UNITS*MAP_TILE_UNITS,a->bounds.south/MAP_TILE_UNITS*MAP_TILE_UNITS,
                    a->bounds,-2,&record,sizeof(record),offsets,4*a->count);
    }
    for (size_t i=0; i<place_count; ++i) {
        struct place_feature p = places[place_index[i].index];
        int x=p.x/MAP_TILE_UNITS*MAP_TILE_UNITS, y=p.y/MAP_TILE_UNITS*MAP_TILE_UNITS;
        /* The instrument's longest label is 47 UTF-8 bytes. Keep one more complete
         * character so the renderer can still indicate truncation. */
        char text[54] = {0};
        const char *name = p.name ? names+p.name : "";
        size_t n=strlen(name);
        if (n>sizeof(text)-2) n=sizeof(text)-2;
        while (n && ((unsigned char)name[n]&0xc0)==0x80) --n;
        memcpy(text,name,n);
        uint16_t bytes=(n+2)&~1u;
        struct map_place record={p.x-x,p.y-y,p.kind,p.min_zoom,bytes};
        pack_record(MAP_PLACE_TREE+p.kind,x,y,(struct map_bounds){p.x,p.y,p.x,p.y},p.min_zoom,&record,sizeof(record),text,bytes);
    }
    fprintf(stderr, "Garmin areas: %zu, vertices: %zu, place anchors: %zu\n", area_count,vertex_count,place_count);
    return area_count + place_count;
}
