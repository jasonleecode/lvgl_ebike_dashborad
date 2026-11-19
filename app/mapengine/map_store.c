#include "map_store.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct cached_page {
    struct cached_page *newer, *older;
    struct map_ref ref;
    unsigned pins;
    uint32_t data[]; /* 4-byte alignment for both directory and feature records. */
};
enum { MAX_DEPTH = 8 };
_Static_assert(EBIKE_MAP_VIEW_REFS >= MAP_TREE_COUNT, "Need at least one reference per layer");
_Static_assert(EBIKE_MAP_CACHE_BYTES >= MAX_DEPTH * (sizeof(struct map_page) + MAP_FANOUT * sizeof(struct map_ref) + sizeof(struct cached_page)) + MAP_PAGE_MAX + sizeof(struct cached_page),
    "Tile cache must hold one pinned search path");

static bool valid_ref(const struct map_ref *r, uint32_t size)
{
    return r->offset >= sizeof(struct map_header) && r->offset <= size &&
           r->size >= sizeof(struct map_page) && r->size <= MAP_PAGE_MAX && r->size <= size - r->offset &&
           r->depth <= MAX_DEPTH && r->bounds.west <= r->bounds.east && r->bounds.south <= r->bounds.north;
}

static bool read_at(int fd, uint32_t offset, void *buf, size_t bytes)
{
    if (lseek(fd, offset, SEEK_SET) != (off_t)offset) return false;
    unsigned char *p = buf;
    while (bytes) {
        ssize_t n = read(fd, p, bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        bytes -= (size_t)n;
    }
    return true;
}

bool map_store_open(struct map_store *s, const char *path)
{
    if (s->ready) return true;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat info;
    struct map_header h;
    bool valid = fstat(fd, &info) == 0 && read_at(fd, 0, &h, sizeof(h)) && !memcmp(h.magic, MAP_MAGIC, 8) &&
        info.st_size == (off_t)h.size && h.checksum == map_checksum(&h, offsetof(struct map_header, checksum));
    for (unsigned i = 0; valid && i < MAP_TREE_COUNT; ++i)
        if (h.roots[i].size) valid = valid_ref(&h.roots[i], h.size);
    if (!valid) {
        close(fd);
        return false;
    }
    s->header = h;
    s->fd = fd;
    s->ready = true;
    return true;
}

static void unlink_page(struct map_store *s, struct cached_page *p)
{
    if (p->newer) p->newer->older = p->older;
    else s->newest = p->older;
    if (p->older) p->older->newer = p->newer;
    else s->oldest = p->newer;
}

static void make_newest(struct map_store *s, struct cached_page *p)
{
    p->newer = NULL;
    p->older = s->newest;
    if (s->newest) s->newest->newer = p;
    else s->oldest = p;
    s->newest = p;
}

void map_store_close(struct map_store *s)
{
    if (s->ready) close(s->fd);
    while (s->newest) {
        struct cached_page *p = s->newest;
        s->newest = p->older;
        free(p);
    }
    free(s->visible);
    s->visible = NULL;
    s->selected = false;
    s->capacity = 0;
    s->count = 0;
    s->oldest = NULL;
    s->resident = 0;
    s->ready = false;
}

static bool validate_page(const struct map_store *s, const struct map_ref *r, unsigned layer,
    const struct map_page *page)
{
    const unsigned char *p = (const unsigned char *)(page + 1);
    size_t left = r->size - sizeof(*page);
    if (!page->count) return false;
    if (r->depth) {
        if (page->type != MAP_BRANCH || page->count > MAP_FANOUT || left != page->count * sizeof(*r)) return false;
        const struct map_ref *children = (const struct map_ref *)p;
        for (unsigned i = 0; i < page->count; ++i)
            if (!valid_ref(&children[i], s->header.size) || children[i].depth + 1 != r->depth ||
                children[i].offset >= r->offset) return false;
        return true;
    }
    if (page->type != layer) return false;
    if (layer == MAP_LINE) return left == page->count * sizeof(struct map_line);
    for (unsigned i = 0; i < page->count; ++i) {
        size_t bytes;
        if (layer == MAP_AREA) {
            if (left < sizeof(struct map_area)) return false;
            const struct map_area *a = (const struct map_area *)p;
            if (a->count < 3) return false;
            bytes = sizeof(*a) + 4u * a->count;
        }
        else {
            if (left < sizeof(struct map_place)) return false;
            const struct map_place *place = (const struct map_place *)p;
            bytes = sizeof(*place) + place->text_bytes;
            if (bytes > left || place->kind > PLACE_OTHER || !place->text_bytes ||
                (place->text_bytes & 1) || p[bytes - 1] != 0) return false;
        }
        if (bytes > left) return false;
        p += bytes;
        left -= bytes;
    }
    return left == 0;
}

static struct cached_page *acquire(struct map_store *s, const struct map_ref *r, unsigned layer)
{
    for (struct cached_page *p = s->newest; p; p = p->older) {
        if (p->ref.offset != r->offset) continue;
        if (p->ref.checksum != r->checksum || p->ref.size != r->size ||
            ((const struct map_page *)p->data)->type != (r->depth ? MAP_BRANCH : layer)) return NULL;
        unlink_page(s, p);
        make_newest(s, p);
        ++p->pins;
        ++s->hits;
        return p;
    }
    size_t cost = sizeof(struct cached_page) + r->size;
    while (s->resident + cost > EBIKE_MAP_CACHE_BYTES) {
        struct cached_page *p = s->oldest;
        while (p && p->pins) p = p->newer;
        if (!p) return NULL;
        unlink_page(s, p);
        s->resident -= sizeof(*p) + p->ref.size;
        free(p);
        ++s->evictions;
    }
    struct cached_page *p = malloc(cost);
    if (!p) return NULL;
    if (!read_at(s->fd, r->offset, p->data, r->size) || map_checksum(p->data, r->size) != r->checksum ||
        !validate_page(s, r, layer, (const struct map_page *)p->data)) {
        free(p);
        return NULL;
    }
    p->ref = *r;
    p->pins = 1;
    make_newest(s, p);
    s->resident += cost;
    if (s->resident > s->peak) s->peak = s->resident;
    ++s->reads;
    return p;
}

enum result { MISS, FOUND, STOP, READ_ERROR, PLAN_FULL };
static enum result visit(struct map_store *s, const struct map_ref *r, unsigned layer, struct map_bounds b,
    int zoom, map_page_cb cb, void *user, int collect_depth)
{
    if (!r->size || zoom < r->min_zoom || !map_intersects(r->bounds, b)) return MISS;
    if (collect_depth >= r->depth) {
        if (s->count == s->capacity) {
            if (s->capacity >= EBIKE_MAP_VIEW_REFS) return PLAN_FULL;
            unsigned cap = s->capacity ? 2 * s->capacity : 32;
            if (cap > EBIKE_MAP_VIEW_REFS) cap = EBIKE_MAP_VIEW_REFS;
            struct map_ref *next = realloc(s->visible, cap * sizeof(*next));
            if (!next) return PLAN_FULL;
            s->visible = next;
            s->capacity = cap;
        }
        s->visible[s->count++] = *r;
        return FOUND;
    }
    if (!cb && !r->depth) return STOP; /* Coverage needs only the leaf bounds. */
    struct cached_page *p = acquire(s, r, layer);
    if (!p) return READ_ERROR;
    const struct map_page *page = (const struct map_page *)p->data;
    enum result result = MISS;
    if (r->depth) {
        const struct map_ref *children = (const struct map_ref *)(page + 1);
        for (unsigned i = 0; i < page->count; ++i) {
            enum result child = visit(s, &children[i], layer, b, zoom, cb, user, collect_depth);
            if (child != MISS) result = child;
            if (child == STOP || child == READ_ERROR || child == PLAN_FULL) break;
        }
    }
    else result = cb(page, user) ? STOP : FOUND;
    --p->pins;
    return result;
}

static bool inside(struct map_bounds a, struct map_bounds b)
{
    return a.west >= b.west && a.south >= b.south && a.east <= b.east && a.north <= b.north;
}

/* Discover the working set once, outside the strip loop. A small guard band lets
 * nearby GPS fixes reuse the selection. At overview scales retain subtrees when
 * listing all intersecting leaves would exceed the reference budget. */
void map_store_select(struct map_store *s, struct map_bounds b)
{
    if (!s->ready || (s->selected && inside(b, s->selection))) return;
    s->selected = false;
    int dx = (b.east - b.west) / 8, dy = (b.north - b.south) / 8;
    b.west -= dx;
    b.east += dx;
    b.south -= dy;
    b.north += dy;
    for (int depth = 0; depth <= MAX_DEPTH; ++depth) {
        s->count = 0;
        unsigned i;
        for (i = 0; i < MAP_LAYER_COUNT; ++i) {
            s->start[i] = s->count;
            enum result res = MISS;
            for (unsigned t = map_tree_begin(i); t < map_tree_end(i); ++t) {
                res = visit(s, &s->header.roots[t], i, b, INT8_MAX, NULL, NULL, depth);
                if (res == READ_ERROR) {
                    map_store_close(s);
                    return;
                }
                if (res == PLAN_FULL) break;
            }
            if (res == PLAN_FULL) break;
        }
        if (i == MAP_LAYER_COUNT) {
            s->start[i] = s->count;
            s->selection = b;
            s->selected = true;
            return;
        }
    }
}

bool map_store_visit(struct map_store *s, unsigned layer, struct map_bounds b, int zoom, map_page_cb cb, void *user)
{
    if (!s->ready || layer >= MAP_LAYER_COUNT) return false;
    const struct map_ref *refs = &s->header.roots[map_tree_begin(layer)];
    unsigned count = map_tree_end(layer) - map_tree_begin(layer);
    if (s->selected && inside(b, s->selection)) {
        count = s->start[layer + 1] - s->start[layer];
        if (!count) return false;
        refs = s->visible + s->start[layer];
    }
    bool found = false;
    for (unsigned i = 0; i < count; ++i) {
        enum result result = visit(s, refs + i, layer, b, zoom, cb, user, -1);
        if (result == READ_ERROR) {
            map_store_close(s);
            return false;
        }
        if (result == STOP) return true;
        if (result == FOUND) found = true;
    }
    return found;
}
