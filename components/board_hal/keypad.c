#include "keypad.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"

#if defined(USE_KEYPAD_4X4_GPIO)

static const gpio_num_t row_pins[4] = {KEYPAD_R1, KEYPAD_R2, KEYPAD_R3, KEYPAD_R4};
static const gpio_num_t col_pins[4] = {KEYPAD_C1, KEYPAD_C2, KEYPAD_C3, KEYPAD_C4};

static const char key_map[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

void keypad_init(void) {
    for (int i = 0; i < 4; i++) {
        // Rows as inputs with pull-up
        gpio_reset_pin(row_pins[i]);
        gpio_set_direction(row_pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(row_pins[i], GPIO_PULLUP_ONLY);

        // Columns as outputs, initially HIGH
        gpio_reset_pin(col_pins[i]);
        gpio_set_direction(col_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(col_pins[i], 1);
    }
}

char keypad_get_key(void) {
    for (int c = 0; c < 4; c++) {
        // Set current column LOW
        gpio_set_level(col_pins[c], 0);
        vTaskDelay(pdMS_TO_TICKS(2)); // Short delay to settle

        for (int r = 0; r < 4; r++) {
            if (gpio_get_level(row_pins[r]) == 0) {
                // Key pressed! Wait for release (basic debounce)
                vTaskDelay(pdMS_TO_TICKS(50));
                while (gpio_get_level(row_pins[r]) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                gpio_set_level(col_pins[c], 1); // Reset column
                return key_map[r][c];
            }
        }
        // Reset column HIGH
        gpio_set_level(col_pins[c], 1);
    }
    return 0; // No key pressed
}

#elif defined(USE_KEYPAD_I2C_PCF8574)

// Include I2C drivers here
// #include "driver/i2c.h"

void keypad_init(void) {
    // Initialize I2C bus and PCF8574
}

char keypad_get_key(void) {
    // Poll PCF8574 over I2C
    return 0;
}

#else

void keypad_init(void) { }
char keypad_get_key(void) { return 0; }

#endif // Keypad Selection
