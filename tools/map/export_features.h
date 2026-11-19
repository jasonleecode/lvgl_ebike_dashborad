#ifndef EBIKE_EXPORT_FEATURES_H
#define EBIKE_EXPORT_FEATURES_H
#include <stdio.h>
#include "libgarmin.h"

void collect_features(struct gmap *map, struct gobject *object);
unsigned write_features(void);
#endif
