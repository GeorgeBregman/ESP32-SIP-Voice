#ifndef PHONEBOOK_H
#define PHONEBOOK_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PHONEBOOK_ENTRIES 10
#define MAX_NAME_LEN 32
#define MAX_URI_LEN 64

typedef struct {
    char name[MAX_NAME_LEN];
    char uri[MAX_URI_LEN];
} phonebook_entry_t;

// Initialize phonebook storage
void phonebook_init(void);

// Save an entry at a specific index (0 to MAX_PHONEBOOK_ENTRIES - 1)
bool phonebook_save_entry(uint8_t index, const char* name, const char* uri);

// Load an entry from a specific index
bool phonebook_load_entry(uint8_t index, phonebook_entry_t* entry);

// Clear a specific entry
bool phonebook_clear_entry(uint8_t index);

// Get the total number of entries currently stored
uint8_t phonebook_get_count(void);

#endif // PHONEBOOK_H
