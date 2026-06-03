#ifndef WAKE_WORD_H
#define WAKE_WORD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Wake Word engine (esp-sr WakeNet).
 * 
 * @return true if successful, false otherwise.
 */
bool wake_word_init(void);

/**
 * @brief Feed raw PCM audio samples into the Wake Word engine.
 * 
 * @param data Pointer to 16-bit PCM audio data.
 * @param len Length of the data in bytes.
 * @return true if wake word detected, false otherwise.
 */
bool wake_word_feed(int16_t *data, int len);

#ifdef __cplusplus
}
#endif

#endif // WAKE_WORD_H
