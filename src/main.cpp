#include <Arduino.h>
#include <M5Cardputer.h>
#include <LittleFS.h>
#include <stdio.h>
#include <string.h>
#include "mcm_core/mcm_core.h"
#include "hal/cardputer_display.h"
#include "hal/cardputer_keyboard.h"
#include "hal/tape_storage.h"
#include "hal/http_tape_manager.h"

#ifndef MCM_DEBUG_SERIAL
#define MCM_DEBUG_SERIAL 1
#endif
#ifndef MCM_DISPLAY_TEST_PATTERN
#define MCM_DISPLAY_TEST_PATTERN 0
#endif

namespace {
constexpr uint16_t kInstructionsPerLoop = 128;

bool g_fs_ready = false;
bool g_rom_ready = false;
bool g_tape_store_ready = false;
#if MCM_DEBUG_SERIAL
uint32_t g_last_tape_flush = 0;
char g_serial_line[96] = {};
size_t g_serial_line_len = 0;
#if !MCM_DISPLAY_TEST_PATTERN
uint32_t g_last_report = 0;
#endif
#endif
} // namespace

#if MCM_DEBUG_SERIAL
static void executeTapeSerialCommand(char *line) {
    if (strcmp(line, "TAPE HELP") == 0) {
        Serial.println("TAPE LIST | TAPE STATUS | TAPE MOUNT 0 <name> | TAPE EJECT 0 | TAPE FLUSH");
    } else if (strcmp(line, "TAPE LIST") == 0) {
        mcm_tape_list(Serial);
    } else if (strcmp(line, "TAPE STATUS") == 0) {
        for (uint8_t d = 0; d < 2; ++d) {
            TP &m = d == 0 ? tape0_s : tape1_s;
            Serial.printf("[TAPE] drive=%u mounted=%s name=%s length=%ld head=%d speed=%02X status=%02X\r\n",
                          d, mcm_tape_is_mounted(d) ? "yes" : "no", m.name,
                          m.length, m.w_head, m.speed, m.status);
        }
    } else if (strcmp(line, "TAPE FLUSH") == 0) {
        mcm_tape_flush_all();
        Serial.println("[TAPE] flush requested");
    } else {
        int drive = -1;
        char name[32] = {};
        if (sscanf(line, "TAPE MOUNT %d %31s", &drive, name) == 2 && drive >= 0 && drive < 2) {
            const bool ok = mcm_tape_mount(static_cast<uint8_t>(drive), name);
            Serial.printf("[TAPE] mount drive %d '%s': %s\r\n", drive, name, ok ? "OK" : "FAILED");
        } else if (sscanf(line, "TAPE EJECT %d", &drive) == 1 && drive >= 0 && drive < 2) {
            const bool ok = mcm_tape_eject(static_cast<uint8_t>(drive));
            Serial.printf("[TAPE] eject drive %d: %s\r\n", drive, ok ? "OK" : "FAILED");
        } else if (line[0] != '\0') {
            Serial.printf("[TAPE] unknown command: %s\r\n", line);
        }
    }
}

static void handleTapeSerialCommands() {
    while (Serial.available() > 0) {
        const int input = Serial.read();
        if (input < 0) break;
        if (input == '\r' || input == '\n') {
            if (g_serial_line_len > 0) {
                g_serial_line[g_serial_line_len] = '\0';
                executeTapeSerialCommand(g_serial_line);
                g_serial_line_len = 0;
            }
        } else if (g_serial_line_len + 1 < sizeof(g_serial_line)) {
            g_serial_line[g_serial_line_len++] = static_cast<char>(input);
        } else {
            g_serial_line_len = 0;
            Serial.println("[TAPE] debug command too long");
        }
    }
}
#endif

void setup() {
#if MCM_DEBUG_SERIAL
    Serial.begin(115200);
    delay(150);
#if MCM_DISPLAY_TEST_PATTERN
    Serial.println("\r\n[MCM70] M6 display-test + HTTP manager starting");
#else
    Serial.println("\r\n[MCM70] M6 Self-Scan + HTTP tape manager starting");
#endif
#endif

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);

    g_fs_ready = LittleFS.begin(false);
#if MCM_DEBUG_SERIAL
    Serial.printf("[M6] LittleFS: %s\r\n", g_fs_ready ? "ready" : "mount failed");
#endif
    g_rom_ready = g_fs_ready && mcm_load_roms();
    mcm_core_init();
    g_tape_store_ready = g_fs_ready && mcm_tape_store_init();
    cardputer_keyboard_init();
    cardputer_display_init(g_rom_ready);
    const bool http_ready = mcm_http_tape_manager_begin(g_tape_store_ready);

#if MCM_DEBUG_SERIAL
    Serial.printf("[M5] ROM load: %s\r\n", g_rom_ready ? "OK" : "FAILED");
    Serial.printf("[M6] Internal-flash tape store: %s\r\n", g_tape_store_ready ? "ready" : "FAILED");
    Serial.printf("[M6] HTTP tape manager: %s; free heap=%u bytes\r\n", http_ready ? "ready" : "FAILED", static_cast<unsigned>(ESP.getFreeHeap()));
    Serial.println("[M6] Debug commands: TAPE HELP / LIST / STATUS / MOUNT 0 <name> / EJECT 0 / FLUSH");
#if MCM_DISPLAY_TEST_PATTERN
    Serial.println("[M6] Display test pattern active; CPU will stay stopped.");
#else
    Serial.println("[M6] Press TAB to run; physical keys are mapped into the MCM/70 keyboard matrix.");
#endif
    Serial.println("[M6] Key pulses use the original MCM/70 bounce/debounce sequence.");
#endif
    cardputer_display_render_if_dirty();
    cardputer_display_update_status();
}

void loop() {
    M5Cardputer.update();
#if MCM_DEBUG_SERIAL
    handleTapeSerialCommands();
#endif
    mcm_http_tape_manager_handle_client();
    cardputer_keyboard_poll(g_rom_ready);

#if !MCM_DISPLAY_TEST_PATTERN
    if (mcm_on && g_rom_ready) {
        mcm_cpu_run_slice(kInstructionsPerLoop);
    }
#endif

    // Display work runs after the CPU slice, not from inside runCPU().
    cardputer_display_render_if_dirty();
    cardputer_display_update_status();

#if MCM_DEBUG_SERIAL
#if !MCM_DISPLAY_TEST_PATTERN
    const uint32_t now = millis();
    if (mcm_on && now - g_last_report >= 1000) {
        g_last_report = now;
        Serial.printf("[CPU] PC=%04X OP=%02X steps=%lu\r\n",
                      mcm_cpu_pc(), mcm_cpu_opcode(), static_cast<unsigned long>(mcm_cpu_steps()));
    }
#endif
#endif
#if MCM_DEBUG_SERIAL
    const uint32_t flushNow = millis();
    if (g_tape_store_ready && flushNow - g_last_tape_flush >= 5000) {
        g_last_tape_flush = flushNow;
        mcm_tape_flush_all();
    }
#endif
    delay(1);
}
