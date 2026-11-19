#ifndef EBIKE_MAP_VIEW_H
#define EBIKE_MAP_VIEW_H
#include "telemetry.h"
void map_view_init(void);
void map_view_update(const bike_readings *readings);
#endif
