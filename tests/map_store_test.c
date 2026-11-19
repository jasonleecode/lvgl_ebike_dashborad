/* Run with the generated Beijing package, see README. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../app/mapengine/map_store.h"

static bool count_page(const struct map_page *p, void *user)
{
    unsigned *count = user;
    *count += p->count;
    return false;
}

static unsigned query(struct map_store *s, int x, int y, int width, int height, int zoom)
{
    struct map_bounds b = {x - width / 2, y - height / 2, x + width / 2, y + height / 2};
    map_store_select(s, b);
    unsigned count = 0;
    for (unsigned i = 0; i < MAP_LAYER_COUNT; ++i) map_store_visit(s, i, b, zoom, count_page, &count);
    assert(s->ready && s->resident <= EBIKE_MAP_CACHE_BYTES);
    return count;
}

/* Corrupt one page after opening the package, like a failed read from removable media. */
static void check_read_failure(void)
{
    char path[] = "/tmp/ebike-tile-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    struct map_store store = {0};
    assert(!map_store_open(&store, path));
    struct map_header header = {.magic = MAP_MAGIC};
    struct map_page page = {.type = MAP_LINE, .count = 1};
    struct map_line line = {0, 0, 10, 10, 1};
    unsigned char bytes[sizeof(page) + sizeof(line)];
    memcpy(bytes, &page, sizeof(page));
    memcpy(bytes + sizeof(page), &line, sizeof(line));
    struct map_bounds bounds = {0, 0, 10, 10};
    struct map_ref ref = {.bounds = bounds, .min_zoom = -2, .offset = sizeof(header), .size = sizeof(bytes),
                          .checksum = map_checksum(bytes, sizeof(bytes))};
    header.roots[MAP_LINE_TREE] = ref;
    header.size = sizeof(header) + sizeof(bytes);
    header.checksum = map_checksum(&header, offsetof(struct map_header, checksum));
    assert(write(fd, &header, sizeof(header)) == sizeof(header));
    assert(write(fd, bytes, sizeof(bytes)) == sizeof(bytes));
    assert(map_store_open(&store, path));
    unsigned count = 0;
    unsigned char bad = 0xff;
    assert(pwrite(fd, &bad, 1, ref.offset) == 1);
    assert(!map_store_visit(&store, MAP_LINE, bounds, 0, count_page, &count));
    assert(!store.ready && !store.resident && !count);
    assert(pwrite(fd, bytes, sizeof(bytes), ref.offset) == sizeof(bytes));
    assert(map_store_open(&store, path));
    assert(map_store_visit(&store, MAP_LINE, bounds, 0, count_page, &count) && count == 1);
    map_store_close(&store);
    assert(ftruncate(fd, ref.offset) == 0);
    assert(!map_store_open(&store, path));
    close(fd);
    unlink(path);
}

int main(int argc, char * *argv)
{
    assert(argc == 2);
    check_read_failure();
    struct map_store s = {0};
    assert(!map_store_open(&s, "/nonexistent/ebike.map"));
    assert(map_store_open(&s, argv[1]) && !s.reads && !s.resident);
    int x = 116.397 * (16777216.0 / 360), y = 39.907 * (16777216.0 / 360);
    unsigned first = query(&s, x, y, 520, 400, 0);
    uint32_t cold = s.reads;
    assert(first && cold);
    assert(query(&s, x, y, 520, 400, 0) == first);
#if EBIKE_MAP_CACHE_BYTES >= 96u * 1024 && EBIKE_MAP_VIEW_REFS >= 384
    assert(s.reads == cold);
#endif
    printf("compact: cold %u page reads, repeat %u, resident %zu bytes\n", cold, s.reads - cold, s.resident);
    uint32_t before = s.reads;
    query(&s, x, y, 1250, 400, 0);
    printf("expand: %u additional page reads\n", s.reads - before);
    before = s.reads;
    query(&s, x, y, 80, 25, 4);
    printf("zoom in: %u additional page reads\n", s.reads - before);
    for (int i = 0; i < 40; ++i) query(&s, x + 400 * i, y, 520, 400, 0);
    assert(s.evictions && s.peak <= EBIKE_MAP_CACHE_BYTES);
    assert(query(&s, x, y, 520, 400, 0) == first);
    before = s.reads;
    assert(!query(&s, -100000, -100000, 500, 400, 0) && before == s.reads);
    printf("pan: %u evictions, peak %zu bytes\n", s.evictions, s.peak);
    map_store_close(&s);
    assert(!s.ready && !s.resident && !s.newest && !s.oldest);
    assert(map_store_open(&s, argv[1]) && query(&s, x, y, 520, 400, 0) == first);
    map_store_close(&s);
}
