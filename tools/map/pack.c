#include "pack.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static FILE *file;
static struct map_header header;
static struct {
    struct map_ref *refs;
    size_t count, capacity;
} layers[MAP_TREE_COUNT];
static int current_tree;
static union { uint32_t align;
               unsigned char bytes[MAP_PAGE_MAX];
} buffer;
static struct map_page page;
static struct map_ref leaf;
static size_t used;

static void fail(const char *msg)
{
    fprintf(stderr, "Map export: %s\n", msg);
    exit(1);
}

uint64_t pack_tile_order(int x, int y)
{
    /* Interleave signed tile coordinates so adjacent index nodes stay local. */
    uint32_t a = (uint32_t)(x / MAP_TILE_UNITS + 65536), b = (uint32_t)(y / MAP_TILE_UNITS + 65536);
    uint64_t key = 0;
    for (unsigned i = 0; i < 18; ++i)
        key |= (uint64_t)((a >> i) & 1) << (2 * i) | (uint64_t)((b >> i) & 1) << (2 * i + 1);
    return key;
}

static void extend(struct map_ref *r, const struct map_ref *s)
{
    if (s->bounds.west < r->bounds.west) r->bounds.west = s->bounds.west;
    if (s->bounds.south < r->bounds.south) r->bounds.south = s->bounds.south;
    if (s->bounds.east > r->bounds.east) r->bounds.east = s->bounds.east;
    if (s->bounds.north > r->bounds.north) r->bounds.north = s->bounds.north;
    if (s->min_zoom < r->min_zoom) r->min_zoom = s->min_zoom;
}

static void write_page(struct map_ref *ref, const void *data, size_t bytes)
{
    long pos = ftell(file);
    if (pos < 0 || pos > INT32_MAX - (long)bytes || fwrite(data, 1, bytes, file) != bytes) fail("package write failed");
    ref->offset = pos;
    ref->size = bytes;
    ref->checksum = map_checksum(data, bytes);
}

static void flush_leaf(void)
{
    if (!used) return;
    memcpy(buffer.bytes, &page, sizeof(page));
    write_page(&leaf, buffer.bytes, used);
    unsigned layer = current_tree;
    if (layers[layer].count == layers[layer].capacity) {
        size_t cap = 2 * layers[layer].capacity + 256;
        layers[layer].refs = realloc(layers[layer].refs, cap * sizeof(leaf));
        if (!layers[layer].refs) fail("out of memory");
        layers[layer].capacity = cap;
    }
    layers[layer].refs[layers[layer].count++] = leaf;
    used = 0;
}

void pack_record(int tree, int x, int y, struct map_bounds bounds, int zoom,
    const void *record, size_t bytes, const void *extra, size_t extra_bytes)
{
    size_t size = bytes + extra_bytes;
    if (size + sizeof(page) > MAP_PAGE_MAX) fail("feature exceeds maximum tile page");
    if (used && (current_tree != tree || page.x != x || page.y != y || used + size > MAP_PAGE_MAX))
        flush_leaf();
    struct map_ref ref = {.bounds = bounds, .min_zoom = zoom};
    if (!used) {
        leaf = ref;
        current_tree = tree;
        unsigned layer = tree < MAP_LINE_TREE ? MAP_AREA : tree == MAP_LINE_TREE ? MAP_LINE : MAP_PLACE;
        page = (struct map_page){x, y, layer, 0};
        used = sizeof(page);
    }
    else extend(&leaf, &ref);
    memcpy(buffer.bytes + used, record, bytes);
    if (extra_bytes) memcpy(buffer.bytes + used + bytes, extra, extra_bytes);
    used += size;
    ++page.count;
}

void pack_begin(FILE *out)
{
    file = out;
    memcpy(header.magic, MAP_MAGIC, 8);
    if (fwrite(&header, sizeof(header), 1, file) != 1) fail("header write failed");
}

void pack_finish(void)
{
    flush_leaf();
    for (unsigned layer = 0; layer < MAP_TREE_COUNT; ++layer) {
        struct map_ref *refs = layers[layer].refs;
        size_t n = layers[layer].count;
        while (n > 1) {
            size_t next = 0;
            for (size_t i = 0; i < n;) {
                size_t count = n - i < MAP_FANOUT ? n - i : MAP_FANOUT;
                struct map_ref parent = refs[i];
                ++parent.depth;
                for (size_t j = 1; j < count; ++j) extend(&parent, &refs[i + j]);
                struct map_page branch = {.type = MAP_BRANCH, .count = count};
                memcpy(buffer.bytes, &branch, sizeof(branch));
                memcpy(buffer.bytes + sizeof(branch), refs + i, count * sizeof(parent));
                write_page(&parent, buffer.bytes, sizeof(branch) + count * sizeof(parent));
                refs[next++] = parent;
                i += count;
            }
            n = next;
        }
        if (n) header.roots[layer] = refs[0];
        free(refs);
    }
    header.size = ftell(file);
    header.checksum = map_checksum(&header, offsetof(struct map_header, checksum));
    if (fseek(file, 0, SEEK_SET) || fwrite(&header, sizeof(header), 1, file) != 1) fail("header update failed");
}
