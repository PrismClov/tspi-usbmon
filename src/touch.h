/*
 * SPDX-License-Identifier: MIT
 */
#ifndef UM_TOUCH_H
#define UM_TOUCH_H

#include "usbmon.h"

int um_touch_start(const struct um_config *cfg, const struct um_geom *geom);
void um_touch_stop(void);

#endif
