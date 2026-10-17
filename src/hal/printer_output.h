#pragma once
#include <stddef.h>
#include <stdint.h>

// Bounded text spool for the emulated MCP-132 printer.
// Printable HyType input is preserved as ASCII; unsupported control/glyph
// representations are replaced with '?'. No network I/O occurs in this module.
void mcm_printer_output_init(void);
void mcm_printer_output_command(int code, uint8_t data);
void mcm_printer_output_reset_head(void);
void mcm_printer_output_clear(void);
size_t mcm_printer_output_copy(char *dst, size_t capacity);
