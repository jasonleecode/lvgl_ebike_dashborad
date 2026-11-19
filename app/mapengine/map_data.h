#ifndef EBIKE_MAP_DATA_H
#define EBIKE_MAP_DATA_H
#include <stdint.h>
#include <stddef.h>

/* Little-endian, fixed-width disk format. All pages are independently checked. */
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The map package requires a little-endian target"
#endif
enum { MAP_AREA, MAP_LINE, MAP_PLACE, MAP_LAYER_COUNT, MAP_BRANCH, MAP_PAGE_MAX = 4096 };
#define MAP_MAGIC "EBTILE1"
struct map_bounds { int32_t west, south, east, north;
};
struct map_ref {
    struct map_bounds bounds;
    uint32_t offset, checksum;
    uint16_t size;
    int8_t min_zoom;
    uint8_t depth;
};
/* Separate spatial trees preserve area paint order and label priority without
 * making one directory node span distant tiles at a change of category. */
enum { MAP_LINE_TREE=5, MAP_PLACE_TREE=6, MAP_TREE_COUNT=15, MAP_FANOUT=16 };
struct map_header { char magic[8];
                    uint32_t size;
                    struct map_ref roots[MAP_TREE_COUNT];
                    uint32_t checksum;
};
static inline unsigned map_tree_begin(unsigned layer)
{
    return layer == MAP_AREA ? 0 : layer == MAP_LINE ? MAP_LINE_TREE : MAP_PLACE_TREE;
}
static inline unsigned map_tree_end(unsigned layer)
{
    return layer == MAP_AREA ? MAP_LINE_TREE : layer == MAP_LINE ? MAP_PLACE_TREE : MAP_TREE_COUNT;
}
struct map_page { int32_t x, y;
                  uint16_t type, count;
};
/* The low byte is the Garmin polyline type; old packages contain only that byte. */
enum { MAP_LINE_TYPE = 0xff, MAP_LINE_ONEWAY = 0x100, MAP_LINE_JUNCTION_START = 0x200,
       MAP_LINE_JUNCTION_END = 0x400 };
static inline int map_is_road(int type)
{
    return (type >= 1 && type <= 0x13) || type == 0x16;
}
struct map_line { int16_t x1, y1, x2, y2, kind;
};
struct map_area { struct map_bounds bounds;
                  uint16_t count, kind;
};
struct map_place { int16_t x, y;
                   uint16_t kind;
                   int16_t min_zoom;
                   uint16_t text_bytes;
};
enum place_kind { PLACE_SETTLEMENT, PLACE_PARK, PLACE_WATER, PLACE_TRANSIT, PLACE_MEDICAL,
                  PLACE_LANDMARK, PLACE_PARKING, PLACE_SERVICE, PLACE_OTHER };
_Static_assert(sizeof(struct map_ref) == 28 && sizeof(struct map_page) == 12 &&
    sizeof(struct map_line) == 10 && sizeof(struct map_area) == 20 && sizeof(struct map_place) == 10,
    "Unexpected map record alignment");

static inline uint32_t map_checksum(const void *bytes, size_t length)
{
    const unsigned char *p = bytes;
    uint32_t hash = 2166136261u;
    while (length--) hash = (hash ^ *p++) * 16777619u;
    return hash;
}

static inline int map_intersects(struct map_bounds a, struct map_bounds b)
{
    return a.west <= b.east && a.east >= b.west && a.south <= b.north && a.north >= b.south;
}
#endif
