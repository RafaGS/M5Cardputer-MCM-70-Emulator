#pragma once

#include <stdbool.h>

// Polls the Cardputer's physical keyboard and queues accepted keypresses for
// the emulated MCM/70 keyboard matrix. TAB starts the emulated machine.
void cardputer_keyboard_init(void);
void cardputer_keyboard_poll(bool rom_ready);
