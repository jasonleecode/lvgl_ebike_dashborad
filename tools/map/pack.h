#ifndef EBIKE_PACK_H
#define EBIKE_PACK_H
#include <stdio.h>
#include "../../app/mapengine/map_data.h"

enum { MAP_TILE_UNITS = 256 };
void pack_begin(FILE *out);
void pack_record(int tree, int x, int y, struct map_bounds bounds, int zoom,
    const void *record, size_t bytes, const void *extra, size_t extra_bytes);
void pack_finish(void);
uint64_t pack_tile_order(int x, int y);
#endif
