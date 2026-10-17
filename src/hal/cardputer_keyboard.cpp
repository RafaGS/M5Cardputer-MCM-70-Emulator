#include "cardputer_keyboard.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <string.h>
#include "../mcm_core/mcm_core.h"

#ifndef MCM_DEBUG_SERIAL
#define MCM_DEBUG_SERIAL 1
#endif
#ifndef MCM_DISPLAY_TEST_PATTERN
#define MCM_DISPLAY_TEST_PATTERN 0
#endif

namespace {
// The original desktop emulator adds 100 copies of a key followed by five
// zeros. Its firmware expects this bounce/debounce sequence while scanning
// the keyboard matrix. Use a fixed ring buffer instead of malloc per sample.
constexpr size_t kQueueCapacity = 1024;
constexpr size_t kPressedSamples = 100;
constexpr size_t kReleasedSamples = 5;
constexpr size_t kSamplesPerKey = kPressedSamples + kReleasedSamples;
uint8_t g_queue[kQueueCapacity];
size_t g_head = 0;
size_t g_tail = 0;
size_t g_count = 0;

bool g_previous_chars[128] = {};
bool g_previous_tab = false;
bool g_previous_enter = false;
bool g_previous_backspace = false;
bool g_previous_del = false;

int delQ(void) {
    if (g_count == 0) return 0x00;
    const uint8_t result = g_queue[g_head];
    g_head = (g_head + 1) % kQueueCapacity;
    --g_count;
    return result;
}

bool enqueueBounce(uint8_t key) {
    if (kQueueCapacity - g_count < kSamplesPerKey) {
#if MCM_DEBUG_SERIAL
        mcm_core_printf("[KBD] queue full; dropped key 0x%02X\r\n", key);
#endif
        return false;
    }
    for (size_t i = 0; i < kPressedSamples; ++i) {
        g_queue[g_tail] = key;
        g_tail = (g_tail + 1) % kQueueCapacity;
        ++g_count;
    }
    for (size_t i = 0; i < kReleasedSamples; ++i) {
        g_queue[g_tail] = 0;
        g_tail = (g_tail + 1) % kQueueCapacity;
        ++g_count;
    }
    return true;
}

void feedKey(uint8_t key, bool rom_ready) {
    if (!rom_ready || !mcm_on || !power) return;

    // Match the desktop callback: physical input is accepted only when the
    // emulated CPU has requested a keyboard read.
    if (key_requested == 0) {
#if MCM_DEBUG_SERIAL
        mcm_core_printf("[KBD] discarded 0x%02X: no pending keyboard read\r\n", key);
#endif
        return;
    }
    key_requested = 0;
    if (enqueueBounce(key)) {
#if MCM_DEBUG_SERIAL
        if (key >= 32 && key <= 126)
            mcm_core_printf("[KBD] accepted '%c' (0x%02X), queue=%u\r\n", key, key,
                            static_cast<unsigned>(g_count));
        else
            mcm_core_printf("[KBD] accepted code 0x%02X, queue=%u\r\n", key,
                            static_cast<unsigned>(g_count));
#endif
    }
}

} // namespace

void cardputer_keyboard_init(void) {
    g_head = g_tail = g_count = 0;
    memset(g_previous_chars, 0, sizeof(g_previous_chars));
    g_previous_tab = false;
    g_previous_enter = false;
    g_previous_backspace = false;
    g_previous_del = false;
}

void cardputer_keyboard_poll(bool rom_ready) {
    const auto &keys = M5Cardputer.Keyboard.keysState();
    bool current_chars[128] = {};

    for (char raw : keys.word) {
        const uint8_t c = static_cast<uint8_t>(raw);
        if (c >= 32 && c <= 126) current_chars[c] = true;
    }

    const bool tab_edge = keys.tab && !g_previous_tab;
    if (tab_edge && !mcm_on) {
#if MCM_DISPLAY_TEST_PATTERN
#if MCM_DEBUG_SERIAL
        mcm_core_printf("[KBD] TAB ignored in display-test build\r\n");
#endif
#else
        if (rom_ready && power) {
            mcm_on = 1;
#if MCM_DEBUG_SERIAL
            mcm_core_printf("[KBD] TAB: CPU started\r\n");
#endif
        } else {
#if MCM_DEBUG_SERIAL
            mcm_core_printf("[KBD] TAB ignored: ROM unavailable or power off\r\n");
#endif
        }
#endif
    }

    // Special keys are translated to the same byte values that the desktop
    // GLUT callback passed to keyboard.c/get_row().
    if (keys.enter && !g_previous_enter) {
        feedKey(13, rom_ready);
    }
    if (keys.backspace && !g_previous_backspace) {
        feedKey(keys.ctrl ? 127 : 8, rom_ready);
    }
    if (keys.del && !g_previous_del) {
        feedKey(127, rom_ready);
    }

    // Printable keys. The M5Cardputer library provides the translated ASCII
    // character (lowercase without Shift, uppercase with Shift/Caps Lock).
    // In the original MCM keyboard table, uppercase ASCII codes stand for
    // the corresponding APL/special legends, so do not lowercase the input.
    for (unsigned int c = 32; c <= 126; ++c) {
        if (!current_chars[c] || g_previous_chars[c]) continue;
        uint8_t code = static_cast<uint8_t>(c);
        if (keys.ctrl) {
            // Preserve the original emulator's special CTRL combinations:
            // CTRL+SPACE=2; CTRL+BACKSPACE=127 (handled above); other CTRL
            // combinations use the common CONTROL matrix code 3.
            code = (c == 32) ? 2 : 3;
        }
        feedKey(code, rom_ready);
    }

    // Record physical state even when a press was rejected (the original
    // event callback also discards it at keydown; it is not replayed later).
    memcpy(g_previous_chars, current_chars, sizeof(g_previous_chars));
    g_previous_tab = keys.tab;
    g_previous_enter = keys.enter;
    g_previous_backspace = keys.backspace;
    g_previous_del = keys.del;
}


// Original MCM/70 keyboard-matrix mapping, retained from keyboard.c.
BYTE get_row(BYTE col)
{  
  int key=delQ();
  BYTE row;
  
  row = 0x00;
  switch (key)
    { 
    case 2:  if (col == 0x08) row=0x60; break;   // insert char 
    case 3:                   row=0x40; break;   // CONTROL is pressed 
    case 8:  if (col == 0x01) row=0x02; break;   // char BSP 
    case 10: if (col == 0x02) row=0x01; break;   // char RET (or EXEC)
    case 13: if (col == 0x02) row=0x01; break;   // char RET (or EXEC)
    case 32: if (col == 0x08) row=0x20; break;   // char space 
    case 33: if (col == 0x40) row=0xA0; break;   // APL char .. 
    case 34: if (col == 0x04) row=0x81; break;   // APL char )  
    case 35: if (col == 0x01) row=0xA0; break;   // APL char <  
    case 36: if (col == 0x80) row=0x90; break;   // APL char =<  
    case 37: if (col == 0x01) row=0x90; break;   // APL char =
    case 38: if (col == 0x01) row=0x88; break;   // APL char >
    case 39: if (col == 0x04) row=0x01; break;   // APL char ] 
    case 40: if (col == 0x01) row=0x84; break;   // APL char v
    case 41: if (col == 0x80) row=0x82; break;   // APL char v  
    case 42: if (col == 0x80) row=0x84; break;   // APL char logical &
    case 43: if (col == 0x80) row=0x81; break;   // APL char div 
    case 44: if (col == 0x08) row=0x02; break;   // ,
    case 45: if (col == 0x01) row=0x01; break;   // +
    case 46: if (col == 0x10) row=0x01; break;   // .
    case 47: if (col == 0x08) row=0x01; break;   // /
    case 48: if (col == 0x80) row=0x02; break;   // char 0
    case 49: if (col == 0x40) row=0x20; break;    // char 1
    case 50: if (col == 0x20) row=0x20; break;    // char 2
    case 51: if (col == 0x01) row=0x20; break;    // char 3
    case 52: if (col == 0x80) row=0x10; break;    // char 4
    case 53: if (col == 0x01) row=0x10; break;    // char 5
    case 54: if (col == 0x80) row=0x08; break;    // char 6
    case 55: if (col == 0x01) row=0x08; break;    // char 7
    case 56: if (col == 0x80) row=0x04; break;    // char 8
    case 57: if (col == 0x01) row=0x04; break;    // char 9
    case 58: if (col == 0x20) row=0x81; break;    // APL char (
    case 59: if (col == 0x20) row=0x01; break;    // APL char [  
    case 60: if (col == 0x08) row=0x82; break;    // ;
    case 61: if (col == 0x80) row=0x01; break;    // APL char x (times)
    case 62: if (col == 0x10) row=0x81; break;    // :
    case 63: if (col == 0x08) row=0x81; break;    // slash right
    case 64: if (col == 0x20) row=0xA0; break;    // APL char -
    case 65: if (col == 0x04) row=0xA0; break;    // APL char alpha
    case 66: if (col == 0x10) row=0x84; break;    // char reversed T
    case 67: if (col == 0x10) row=0x88; break;    // char intersection
    case 68: if (col == 0x04) row=0x90; break;   // char L
    case 69: if (col == 0x40) row=0x90; break;   // char membership
    case 70: if (col == 0x20) row=0x88; break;   // char _
    case 71: if (col == 0x04) row=0x88; break;   // char triangle down \/
    case 72: if (col == 0x20) row=0x84; break;   // char triangle up 
    case 73: if (col == 0x02) row=0x84; break;   // char iota
    case 74: if (col == 0x04) row=0x84; break;   // char o
    case 75: if (col == 0x20) row=0x82; break;   // char quote
    case 76: if (col == 0x04) row=0x82; break;   // char quad []
    case 77: if (col == 0x10) row=0x82; break;   // char |
    case 78: if (col == 0x08) row=0x84; break;   // char true small T
    case 79: if (col == 0x40) row=0x82; break;   // char big circ
    case 80: if (col == 0x02) row=0x82; break;   // char *
    case 81: if (col == 0x10) row=0xA0; break;   // char ?
    case 82: if (col == 0x02) row=0x90; break;   // char rho
    case 83: if (col == 0x20) row=0x90; break;   // char top left corner
    case 84: if (col == 0x40) row=0x88; break;   // char ~
    case 85: if (col == 0x40) row=0x84; break;   // char down arrow
    case 86: if (col == 0x08) row=0x88; break;   // char union
    case 87: if (col == 0x02) row=0xA0; break;   // char omega
    case 88: if (col == 0x08) row=0x90; break;   // char back inclusion
    case 89: if (col == 0x02) row=0x88; break;   // char up arrow
    case 90: if (col == 0x10) row=0x90; break;   // char inclusion
    case 91: if (col == 0x40) row=0x01; break;   // char <-
    
    case 94: if (col == 0x80) row=0x88; break;   // APL char >=
    case 95: if (col == 0x01) row=0x81; break;   // APL char -
    case 97: if (col == 0x04) row=0x20; break;    // char A
    case 98: if (col == 0x10) row=0x04; break;    // char B
    case 99: if (col == 0x10) row=0x08; break;    // char C
    case 100: if (col == 0x04) row=0x10; break;   // char D
    case 101: if (col == 0x40) row=0x10; break;   // char E
    case 102: if (col == 0x20) row=0x08; break;   // char F
    case 103: if (col == 0x04) row=0x08; break;   // char G
    case 104: if (col == 0x20) row=0x04; break;   // char H
    case 105: if (col == 0x02) row=0x04; break;   // char I
    case 106: if (col == 0x04) row=0x04; break;   // char J
    case 107: if (col == 0x20) row=0x02; break;   // char K
    case 108: if (col == 0x04) row=0x02; break;   // char L
    case 109: if (col == 0x10) row=0x02; break;   // char M
    case 110: if (col == 0x08) row=0x04; break;   // char N
    case 111: if (col == 0x40) row=0x02; break;   // char O
    case 112: if (col == 0x02) row=0x02; break;   // char P
    case 113: if (col == 0x10) row=0x20; break;   // char Q
    case 114: if (col == 0x02) row=0x10; break;   // char R
    case 115: if (col == 0x20) row=0x10; break;   // char S
    case 116: if (col == 0x40) row=0x08; break;   // char T
    case 117: if (col == 0x40) row=0x04; break;   // char U
    case 118: if (col == 0x08) row=0x08; break;   // char V
    case 119: if (col == 0x02) row=0x20; break;   // char W
    case 120: if (col == 0x08) row=0x10; break;   // char X
    case 121: if (col == 0x02) row=0x08; break;   // char Y
    case 122: if (col == 0x10) row=0x10; break;   // char Z
    case 123: if (col == 0x40) row=0x81; break;   // char ->
    case 127: if (col == 0x01) row=0x42; break;   // DEL char
    default: row=0x00;
    }
 
 return row;
}
