#pragma once

#include <stdbool.h>

// Initializes the Cardputer display chrome. The Self-Scan content is rendered
// from the emulated display memory by cardputer_display_render_if_dirty().
void cardputer_display_init(bool rom_ready);
void cardputer_display_render_if_dirty();
void cardputer_display_update_status();
