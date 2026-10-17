#include "mcm_core.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ctype.h>
#include <stdlib.h>
#ifndef MCM_DEBUG_SERIAL
#define MCM_DEBUG_SERIAL 1
#endif

// ROM source files keep their original text-hex representation.
// Copy ROM6k and ROMs0-C from the original distribution into data/ before uploadfs.
static bool readHexValue(File &file, uint8_t &value) {
    char token[8];
    size_t length = 0;

    while (file.available()) {
        const int next = file.read();
        if (next < 0) break;
        const unsigned char c = static_cast<unsigned char>(next);
        if (isspace(c)) {
            if (length == 0) continue;
            break;
        }
        if (!isxdigit(c) || length >= sizeof(token) - 1) return false;
        token[length++] = static_cast<char>(c);
    }
    if (length == 0) return false;
    token[length] = '\0';
    char *end = nullptr;
    const unsigned long parsed = strtoul(token, &end, 16);
    if (end == token || *end != '\0' || parsed > 0xFFUL) return false;
    value = static_cast<uint8_t>(parsed);
    return true;
}

static bool loadHexFile(const char *path, uint8_t *destination, size_t expectedBytes) {
    File file = LittleFS.open(path, "r");
    if (!file) {
#if MCM_DEBUG_SERIAL
        Serial.printf("[ROM] Cannot open %s\r\n", path);
#endif
        return false;
    }

    for (size_t i = 0; i < expectedBytes; ++i) {
        uint8_t value = 0;
        if (!readHexValue(file, value)) {
#if MCM_DEBUG_SERIAL
            Serial.printf("[ROM] Invalid/truncated hex data in %s at byte %u of %u\r\n",
                          path, static_cast<unsigned>(i), static_cast<unsigned>(expectedBytes));
#endif
            file.close();
            return false;
        }
        destination[i] = value;
    }

    file.close();
    return true;
}

bool mcm_load_roms(void) {
    // Fail closed: if either ROM file is missing or truncated, do not start the CPU.
    for (size_t i = 0; i < sizeof(memory); ++i) memory[i] = 0;
    for (size_t i = 0; i < sizeof(ROM); ++i) ROM[i] = 0;

    const bool baseOk = loadHexFile("/ROM6k", memory, 6144);
    const bool banksOk = loadHexFile("/ROMs0-C", ROM, 32768);
    if (!baseOk || !banksOk) return false;

    ROM_end = 0x1FFF;
#if MCM_DEBUG_SERIAL
    Serial.println("[ROM] Loaded 6144 base-ROM bytes and 32768 bank-ROM bytes");
#endif
    return true;
}
