#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

// Initialize the 4x4 keypad matrix
void keypad_init(void);

// Poll the keypad for a pressed key. Returns the character, or 0 if nothing pressed.
char keypad_get_key(void);

#endif // KEYPAD_H
