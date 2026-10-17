#include "cardputer_display.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include "../mcm_core/mcm_core.h"

#ifndef MCM_DEBUG_SERIAL
#define MCM_DEBUG_SERIAL 1
#endif
#ifndef MCM_DISPLAY_TEST_PATTERN
#define MCM_DISPLAY_TEST_PATTERN 0
#endif

namespace {
constexpr uint16_t kBg = 0x2104;       // dark reddish panel background
constexpr uint16_t kHeader = 0x07FF;   // cyan
constexpr uint16_t kPixelOn = 0xF986;  // bright warm red/orange
constexpr uint16_t kGood = 0x07E0;
constexpr uint16_t kWarn = 0xFFE0;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kMuted = 0xAD55;

constexpr int kDisplayRows = 7;
constexpr int kCellPitch = 7;          // 5 raster columns + 2 inter-character columns
constexpr int kCharsPerRow = 16;
constexpr int kRasterColsPerRow = kCharsPerRow * kCellPitch - 2; // 110 columns
constexpr int kSecondRowStart = kCharsPerRow * kCellPitch;       // column 112
constexpr int kScale = 2;
constexpr int kRasterWidthPx = kRasterColsPerRow * kScale;        // 220 pixels
constexpr int kRasterX = (240 - kRasterWidthPx) / 2;              // 10 pixels
constexpr int kRowY[2] = {48, 70};

bool g_dirty = true;
bool g_rom_ready = false;
uint32_t g_last_status_ms = 0;
bool g_status_drawn = false;

void drawChrome() {
    auto &display = M5Cardputer.Display;
    display.fillScreen(kBg);
    display.setTextSize(1);
    display.setTextColor(kHeader, kBg);
    display.drawString("MCM/70 SELF-SCAN", 4, 3);
#if MCM_DISPLAY_TEST_PATTERN
    display.setTextColor(kWarn, kBg);
    display.drawString("TEST", 202, 3);
#else
    display.setTextColor(g_rom_ready ? kGood : kWarn, kBg);
    display.drawString(g_rom_ready ? "ROM OK" : "NO ROM", 194, 3);
#endif
    display.drawFastHLine(4, 15, 232, kHeader);
    display.setTextColor(kMuted, kBg);
    display.drawString("WiFi MCM70-Cardputer", 5, 98);
    display.drawString("WEB 192.168.4.1", 5, 106);
    display.drawFastHLine(4, 114, 232, kMuted);
}

void drawRasterRow(int row_index, int source_start) {
    auto &display = M5Cardputer.Display;
    const int y_origin = kRowY[row_index];

    // Render the 7 vertical bits in each Self-Scan column. Adjacent ON points
    // are grouped into horizontal runs to reduce TFT drawing calls.
    for (int y_bit = 0; y_bit < kDisplayRows; ++y_bit) {
        int x = 0;
        while (x < kRasterColsPerRow) {
            const uint8_t bits = memory[0x2021 + source_start + x];
            const bool on = (bits & (1u << y_bit)) != 0;
            if (!on) {
                ++x;
                continue;
            }

            const int run_start = x;
            do {
                ++x;
                if (x >= kRasterColsPerRow) break;
            } while ((memory[0x2021 + source_start + x] & (1u << y_bit)) != 0);

            display.fillRect(kRasterX + run_start * kScale,
                             y_origin + y_bit * kScale,
                             (x - run_start) * kScale,
                             kScale,
                             kPixelOn);
        }
    }
}

#if MCM_DEBUG_SERIAL && MCM_DISPLAY_TEST_PATTERN
void makeDisplayTestPattern() {
    // Frame-like 5x7 glyphs with two blank columns between characters.
    // This is test-only data, installed after reset, to validate column order,
    // 16+16 splitting, centering, and 2x scaling without needing a ROM image.
    for (int col = 0; col < 222; ++col) {
        const int glyph_x = col % kCellPitch;
        const int glyph_index = col / kCellPitch;
        uint8_t bits = 0;
        if (glyph_x < 5) {
            for (int y = 0; y < kDisplayRows; ++y) {
                bool on = (y == 0 || y == 6 || glyph_x == 0 || glyph_x == 4);
                if ((glyph_index & 1) && glyph_x == 2 && y >= 2 && y <= 4) on = true;
                if (on) bits |= static_cast<uint8_t>(1u << y);
            }
        }
        memory[0x2021 + col] = bits;
    }
    refresh_SS();
}
#endif
} // namespace

void cardputer_display_init(bool rom_ready) {
    g_rom_ready = rom_ready;
    drawChrome();
    g_dirty = true;
    g_status_drawn = false;
    g_last_status_ms = 0;
#if MCM_DEBUG_SERIAL && MCM_DISPLAY_TEST_PATTERN
    makeDisplayTestPattern();
    Serial.println("[M3] Display test pattern installed; CPU start is disabled in this build.");
#endif
}

void cardputer_display_render_if_dirty() {
    if (!g_dirty) return;
    g_dirty = false;

    auto &display = M5Cardputer.Display;
    display.fillRect(4, 20, 232, 76, kBg);
    drawRasterRow(0, 0);
    drawRasterRow(1, kSecondRowStart);
}

void cardputer_display_update_status() {
    const uint32_t now = millis();
    if (g_status_drawn && now - g_last_status_ms < 250) return;
    g_status_drawn = true;
    g_last_status_ms = now;

    auto &display = M5Cardputer.Display;
    display.fillRect(4, 116, 232, 17, kBg);
    display.setTextSize(1);
#if MCM_DISPLAY_TEST_PATTERN
    display.setTextColor(kWarn, kBg);
    display.drawString("DISPLAY TEST  CPU STOPPED", 5, 120);
#else
    display.setTextColor(mcm_on ? kGood : kText, kBg);
    char status[40];
    snprintf(status, sizeof(status), "%s PC:%04X S:%06lu",
             mcm_on ? "RUN" : "STOP",
             mcm_cpu_pc(),
             static_cast<unsigned long>(mcm_cpu_steps() % 1000000UL));
    display.drawString(status, 5, 120);
#endif
}

// These functions replace the desktop OpenGL implementation in display.c.
// The CPU core continues to call the same interface; rendering happens after
// each CPU slice in loop(), avoiding TFT work in the middle of CPU execution.
void refresh_SS(void) {
    refresh_display = 0;
    g_dirty = true;
}

void clear_display(void) {
    refresh_display = 0;
    g_dirty = true;
}
