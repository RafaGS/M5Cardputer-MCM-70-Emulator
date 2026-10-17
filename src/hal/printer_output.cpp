#include "printer_output.h"
#include <string.h>

namespace {
constexpr size_t kColumns = 68;
constexpr size_t kRows = 128;
constexpr int kLeftMarginPx = 64;
constexpr int kRightLimitPx = 900;
constexpr int kCharWidthPx = 12;
constexpr int kLineFeedPx = 16;
char g_page[kRows][kColumns];
int g_x_px = 76;
int g_feed_remainder = 0;

void clearRow(size_t row) { memset(g_page[row], ' ', kColumns); }

void scrollUp() {
    memmove(g_page[0], g_page[1], (kRows - 1) * kColumns);
    clearRow(kRows - 1);
}
void scrollDown() {
    memmove(g_page[1], g_page[0], (kRows - 1) * kColumns);
    clearRow(0);
}

void feedPaper(int pixels, bool down) {
    if (pixels <= 0) return;
    g_feed_remainder += pixels;
    while (g_feed_remainder >= kLineFeedPx) {
        if (down) scrollDown(); else scrollUp();
        g_feed_remainder -= kLineFeedPx;
    }
}

char printableHyType(uint8_t data) {
    const unsigned int ch = data / 2U;
    // This is a plain-text export, not a pixel-perfect APL/HyType rendering.
    // Keep normal ASCII useful; represent unsupported codes visibly.
    if (ch >= 32U && ch <= 126U) return static_cast<char>(ch);
    return '?';
}

size_t firstUsedRow() {
    for (size_t r = 0; r < kRows; ++r)
        for (size_t c = 0; c < kColumns; ++c)
            if (g_page[r][c] != ' ') return r;
    return kRows - 1;
}
size_t lastUsedRow() {
    for (size_t r = kRows; r > 0; --r)
        for (size_t c = 0; c < kColumns; ++c)
            if (g_page[r - 1][c] != ' ') return r - 1;
    return kRows - 1;
}
} // namespace

void mcm_printer_output_init(void) {
    memset(g_page, ' ', sizeof(g_page));
    g_x_px = 76;
    g_feed_remainder = 0;
}

void mcm_printer_output_reset_head(void) {
    g_x_px = kLeftMarginPx;
}

void mcm_printer_output_clear(void) {
    memset(g_page, ' ', sizeof(g_page));
    g_x_px = kLeftMarginPx;
    g_feed_remainder = 0;
}

void mcm_printer_output_command(int code, uint8_t data) {
    if (code < 0) return;
    const unsigned int op = (static_cast<unsigned int>(code) >> 5U) & 0x07U;
    const int amount = static_cast<int>(data) +
                       (static_cast<int>(code & 0x07) * 256);
    switch (op) {
        case 1: { // Print at the current carriage position.
            int col = (g_x_px - kLeftMarginPx) / kCharWidthPx;
            if (col < 0) col = 0;
            if (col >= static_cast<int>(kColumns)) col = static_cast<int>(kColumns) - 1;
            g_page[kRows - 1][static_cast<size_t>(col)] = printableHyType(data);
            break;
        }
        case 2: // Carriage movement, measured in printer pixels.
            if ((code & 0x08) == 0) {
                g_x_px += amount;
                if (g_x_px > kRightLimitPx) {
                    scrollUp();
                    g_x_px = kLeftMarginPx;
                }
            } else {
                g_x_px -= amount;
                if (g_x_px < kLeftMarginPx) g_x_px = kLeftMarginPx;
            }
            break;
        case 4: // Paper feed; positive direction moves existing text up.
            feedPaper(amount, (code & 0x08) != 0);
            break;
        case 7: // Printer reset restores the carriage/status, not the spool.
            mcm_printer_output_reset_head();
            break;
        default:
            // Other original printer operations (ribbon, platen, etc.) are
            // not represented in the text spool.
            break;
    }
}

size_t mcm_printer_output_copy(char *dst, size_t capacity) {
    if (!dst || capacity == 0) return 0;
    const size_t first = firstUsedRow();
    const size_t last = lastUsedRow();
    size_t out = 0;
    for (size_t r = first; r <= last; ++r) {
        size_t end = kColumns;
        while (end > 0 && g_page[r][end - 1] == ' ') --end;
        const size_t need = end + (r < last ? 1U : 0U);
        if (out + need >= capacity) break;
        if (end) { memcpy(dst + out, g_page[r], end); out += end; }
        if (r < last) dst[out++] = '\n';
    }
    dst[out] = '\0';
    return out;
}


