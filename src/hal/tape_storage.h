#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Tape images are stored in LittleFS as compact M70T files (12-byte header
// followed by uint16_t little-endian values). The original .tp hexadecimal
// representation is accepted by the streaming importer and exportable again.

bool mcm_tape_store_init(void);
bool mcm_tape_is_mounted(uint8_t drive);
uint16_t mcm_tape_read(uint8_t drive, uint32_t index);
bool mcm_tape_write(uint8_t drive, uint32_t index, uint16_t value);
bool mcm_tape_mount(uint8_t drive, const char *name);
bool mcm_tape_eject(uint8_t drive);
// Delete an unmounted tape image by its base name (without .tp/.mct).
bool mcm_tape_delete(const char *name);
bool mcm_tape_flush(uint8_t drive);
void mcm_tape_flush_all(void);
void mcm_tape_list(Print &out);
bool mcm_tape_export_text(uint8_t drive, Print &out);

// Stream an uploaded .tp file in chunks without retaining the whole upload in RAM.
bool mcm_tape_import_begin(const char *filename);
bool mcm_tape_import_chunk(const uint8_t *data, size_t length);
bool mcm_tape_import_finish(void);
void mcm_tape_import_cancel(void);
