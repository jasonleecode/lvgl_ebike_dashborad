#ifndef EBIKE_MAP_STORE_H
#define EBIKE_MAP_STORE_H
#include <stdbool.h>
#include "map_data.h"

#ifndef EBIKE_MAP_CACHE_BYTES
#define EBIKE_MAP_CACHE_BYTES (96u * 1024u)
#endif

#ifndef EBIKE_MAP_VIEW_REFS
#define EBIKE_MAP_VIEW_REFS 384u
#endif

struct cached_page;
struct map_store {
    struct map_header header;
    struct cached_page *newest, *oldest;
    size_t resident, peak;
    uint32_t reads, hits, evictions;
    int fd;
    bool ready, selected;
    struct map_bounds selection;
    struct map_ref *visible;
    unsigned capacity, count, start[MAP_LAYER_COUNT + 1];
};
/* Page and record pointers are only valid for the duration of the callback.
 * Return true to stop a spatial search after the current page. */
typedef bool (*map_page_cb)(const struct map_page *, void *);
bool map_store_open(struct map_store *, const char *path);
void map_store_close(struct map_store *);
void map_store_select(struct map_store *, struct map_bounds viewport);
bool map_store_visit(struct map_store *, unsigned layer, struct map_bounds, int zoom, map_page_cb, void *);
#endif
