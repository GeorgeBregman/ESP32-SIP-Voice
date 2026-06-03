#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void touch_driver_init(void);
bool touch_driver_read(int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_DRIVER_H
