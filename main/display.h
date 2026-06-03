#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"

// Initialize the OLED display (using default I2C pins if shared, or define new ones)
esp_err_t display_init(void);

// Clear the display
void display_clear(void);

// Print text at line (0 to 7)
void display_print_line(int line, const char *text);

// Show IP Address, Status, Caller
void display_update_status(const char *ip, const char *sip_status, const char *caller);

#endif
