#include "tape_storage.h"

#include <LittleFS.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../mcm_core/mcm_core.h"

namespace {
constexpr uint32_t kHeaderSize = 12;
constexpr uint16_t kFormatVersion = 1;
constexpr uint16_t kRecordSize = 2;
constexpr uint16_t kPageEntries = 256;
constexpr uint8_t kCachePages = 4;
constexpr uint32_t kMaxTapeEntries = 1000000UL;
constexpr uint16_t kBlank = 0x0100;
constexpr size_t kWriteBufferSize = 512;

struct CachePage {
    bool valid = false;
    bool dirty = false;
    uint32_t pageIndex = 0;
    uint32_t age = 0;
    uint16_t entries[kPageEntries] = {};
};

struct TapeDrive {
    File file;
    bool mounted = false;
    uint32_t length = 0;
    uint32_t useClock = 0;
    char baseName[16] = {};
    CachePage cache[kCachePages];
};

struct ImportState {
    File file;
    bool active = false;
    bool failed = false;
    char baseName[16] = {};
    char token[12] = {};
    size_t tokenLength = 0;
    uint32_t count = 0;
    uint8_t output[kWriteBufferSize] = {};
    size_t outputUsed = 0;
};

TapeDrive g_drives[2];
ImportState g_import;
bool g_storeReady = false;

void put16(uint8_t *p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }
void put32(uint8_t *p, uint32_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff; }
uint32_t get32(const uint8_t *p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }

bool safeBaseName(const char *input, char output[16]) {
    if (!input || !*input) return false;
    // Accept browser fake paths defensively, but only preserve the final basename.
    const char *base = input;
    for (const char *p = input; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    char temp[32] = {};
    if (strlen(base) >= sizeof(temp)) return false;
    strcpy(temp, base);
    size_t n = strlen(temp);
    if (n >= 4 && strcasecmp(temp + n - 4, ".mct") == 0) temp[n - 4] = '\0';
    else if (n >= 3 && strcasecmp(temp + n - 3, ".tp") == 0) temp[n - 3] = '\0';
    n = strlen(temp);
    // TP.name is 20 bytes in the original model. Leave room for the .tp suffix.
    if (n == 0 || n > 15 || temp[0] == '.') return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(temp[i]);
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return false;
    }
    memcpy(output, temp, n + 1);
    return true;
}

void makePath(const char *base, char path[40]) { snprintf(path, 40, "/tapes/%s.mct", base); }

bool readHeader(File &file, uint32_t &count) {
    uint8_t header[kHeaderSize];
    if (!file || file.size() < kHeaderSize || !file.seek(0, SeekSet)) return false;
    if (file.read(header, sizeof(header)) != sizeof(header)) return false;
    if (memcmp(header, "M70T", 4) != 0 || get16(header + 4) != kFormatVersion || get16(header + 6) != kRecordSize) return false;
    count = get32(header + 8);
    if (count == 0 || count > kMaxTapeEntries) return false;
    return file.size() == kHeaderSize + static_cast<uint64_t>(count) * kRecordSize;
}

bool flushPage(TapeDrive &drive, CachePage &page) {
    if (!page.valid || !page.dirty) return true;
    if (!drive.file) return false;
    const uint32_t first = page.pageIndex * kPageEntries;
    if (first >= drive.length) { page.dirty = false; return true; }
    const uint16_t entries = static_cast<uint16_t>((drive.length - first) < kPageEntries ? (drive.length - first) : kPageEntries);
    uint8_t raw[kPageEntries * 2];
    for (uint16_t i = 0; i < entries; ++i) put16(raw + i * 2, page.entries[i]);
    const uint32_t offset = kHeaderSize + first * 2;
    if (!drive.file.seek(offset, SeekSet)) return false;
    if (drive.file.write(raw, entries * 2) != entries * 2) return false;
    page.dirty = false;
    return true;
}

CachePage *getPage(TapeDrive &drive, uint32_t index) {
    if (!drive.mounted || index >= drive.length) return nullptr;
    const uint32_t pageIndex = index / kPageEntries;
    for (uint8_t i = 0; i < kCachePages; ++i) {
        if (drive.cache[i].valid && drive.cache[i].pageIndex == pageIndex) {
            drive.cache[i].age = ++drive.useClock;
            return &drive.cache[i];
        }
    }
    CachePage *victim = nullptr;
    for (uint8_t i = 0; i < kCachePages; ++i) {
        if (!drive.cache[i].valid) { victim = &drive.cache[i]; break; }
        if (!victim || drive.cache[i].age < victim->age) victim = &drive.cache[i];
    }
    if (!victim || !flushPage(drive, *victim)) return nullptr;
    victim->valid = false;
    victim->dirty = false;
    for (uint16_t i = 0; i < kPageEntries; ++i) victim->entries[i] = kBlank;
    const uint32_t first = pageIndex * kPageEntries;
    const uint16_t entries = static_cast<uint16_t>((drive.length - first) < kPageEntries ? (drive.length - first) : kPageEntries);
    const size_t bytesExpected = static_cast<size_t>(entries) * 2;
    uint8_t raw[kPageEntries * 2];
    if (!drive.file.seek(kHeaderSize + first * 2, SeekSet)) return nullptr;
    if (drive.file.read(raw, bytesExpected) != bytesExpected) return nullptr;
    for (uint16_t i = 0; i < entries; ++i) victim->entries[i] = get16(raw + i * 2);
    victim->pageIndex = pageIndex;
    victim->valid = true;
    victim->dirty = false;
    victim->age = ++drive.useClock;
    return victim;
}

bool flushImportBuffer() {
    if (!g_import.active || g_import.outputUsed == 0) return true;
    if (g_import.file.write(g_import.output, g_import.outputUsed) != g_import.outputUsed) {
        g_import.failed = true;
        return false;
    }
    g_import.outputUsed = 0;
    return true;
}

bool emitImportedToken() {
    if (g_import.tokenLength == 0) return true;
    g_import.token[g_import.tokenLength] = '\0';
    char *end = nullptr;
    const unsigned long value = strtoul(g_import.token, &end, 16);
    if (end == g_import.token || *end != '\0' || value > kBlank || g_import.count >= kMaxTapeEntries) {
        g_import.failed = true;
        return false;
    }
    if (g_import.outputUsed + 2 > sizeof(g_import.output) && !flushImportBuffer()) return false;
    g_import.output[g_import.outputUsed++] = static_cast<uint8_t>(value & 0xff);
    g_import.output[g_import.outputUsed++] = static_cast<uint8_t>((value >> 8) & 0xff);
    ++g_import.count;
    g_import.tokenLength = 0;
    return true;
}

void writeHeader(uint8_t header[kHeaderSize], uint32_t count) {
    memcpy(header, "M70T", 4);
    put16(header + 4, kFormatVersion);
    put16(header + 6, kRecordSize);
    put32(header + 8, count);
}

void clearDriveCache(TapeDrive &drive) {
    for (uint8_t i = 0; i < kCachePages; ++i) {
        drive.cache[i].valid = false;
        drive.cache[i].dirty = false;
        drive.cache[i].pageIndex = 0;
        drive.cache[i].age = 0;
    }
    drive.useClock = 0;
}

} // namespace

bool mcm_tape_store_init(void) {
    if (!LittleFS.exists("/tapes") && !LittleFS.mkdir("/tapes")) {
        g_storeReady = false;
        return false;
    }
    for (uint8_t d = 0; d < 2; ++d) {
        if (g_drives[d].mounted) {
            (void)mcm_tape_flush(d);
            g_drives[d].file.close();
        }
        g_drives[d].mounted = false;
        g_drives[d].length = 0;
        g_drives[d].baseName[0] = '\0';
        clearDriveCache(g_drives[d]);
    }
    g_storeReady = true;
    return true;
}

bool mcm_tape_is_mounted(uint8_t drive) {
    return drive < 2 && g_drives[drive].mounted && static_cast<bool>(g_drives[drive].file);
}

uint16_t mcm_tape_read(uint8_t drive, uint32_t index) {
    if (drive >= 2) return kBlank;
    CachePage *page = getPage(g_drives[drive], index);
    if (!page) return kBlank;
    return page->entries[index % kPageEntries];
}

bool mcm_tape_write(uint8_t drive, uint32_t index, uint16_t value) {
    if (drive >= 2) return false;
    CachePage *page = getPage(g_drives[drive], index);
    if (!page) return false;
    page->entries[index % kPageEntries] = value;
    page->dirty = true;
    return true;
}

bool mcm_tape_mount(uint8_t drive, const char *name) {
    if (!g_storeReady || drive >= 2 || g_drives[drive].mounted) return false;
    char base[16];
    if (!safeBaseName(name, base)) return false;
    char path[40];
    makePath(base, path);
    File file = LittleFS.open(path, "r+");
    if (!file) return false;
    uint32_t count = 0;
    if (!readHeader(file, count)) { file.close(); return false; }

    TapeDrive &d = g_drives[drive];
    d.file = file;
    d.mounted = true;
    d.length = count;
    strcpy(d.baseName, base);
    clearDriveCache(d);

    TP &meta = (drive == 0) ? tape0_s : tape1_s;
    meta.id = (drive == 0) ? 0xC8 : 0xC9;
    meta.status = 0xC0;
    meta.lid = 0;
    meta.w_head = delta;
    meta.speed = 0;
    meta.length = static_cast<long int>(count);
    snprintf(meta.name, sizeof(meta.name), "%s.tp", base);
    return true;
}

bool mcm_tape_delete(const char *name) {
    if (!g_storeReady) return false;
    char base[16];
    if (!safeBaseName(name, base)) return false;
    for (uint8_t d = 0; d < 2; ++d) {
        if (g_drives[d].mounted && strcmp(g_drives[d].baseName, base) == 0) return false;
    }
    char path[40];
    makePath(base, path);
    if (!LittleFS.exists(path)) return false;
    return LittleFS.remove(path);
}

bool mcm_tape_eject(uint8_t drive) {
    if (drive >= 2 || !g_drives[drive].mounted) return false;
    TapeDrive &d = g_drives[drive];
    if (!mcm_tape_flush(drive)) return false;
    d.file.close();
    d.mounted = false;
    d.length = 0;
    d.baseName[0] = '\0';
    clearDriveCache(d);
    TP &meta = (drive == 0) ? tape0_s : tape1_s;
    meta.status = 13;
    meta.length = 0;
    meta.w_head = delta;
    meta.speed = 0;
    meta.name[0] = '\0';
    return true;
}

bool mcm_tape_flush(uint8_t drive) {
    if (drive >= 2 || !g_drives[drive].mounted) return false;
    TapeDrive &d = g_drives[drive];
    for (uint8_t i = 0; i < kCachePages; ++i) if (!flushPage(d, d.cache[i])) return false;
    d.file.flush();
    return true;
}

void mcm_tape_flush_all(void) {
    for (uint8_t d = 0; d < 2; ++d) if (g_drives[d].mounted) (void)mcm_tape_flush(d);
}

void mcm_tape_list(Print &out) {
    if (!g_storeReady) { out.println("[TAPE] LittleFS store unavailable"); return; }
    File directory = LittleFS.open("/tapes", "r");
    if (!directory || !directory.isDirectory()) { out.println("[TAPE] no /tapes directory"); return; }
    bool found = false;
    File item = directory.openNextFile();
    while (item) {
        const char *name = item.name();
        const size_t n = name ? strlen(name) : 0;
        if (!item.isDirectory() && n >= 4 && strcmp(name + n - 4, ".mct") == 0) {
            found = true;
            const char *displayName = name;
            const char *slash = strrchr(name, '/');
            if (slash) displayName = slash + 1;
            out.print("[TAPE] "); out.print(displayName); out.print(" ("); out.print(static_cast<unsigned>(item.size())); out.println(" bytes)");
        }
        item.close();
        item = directory.openNextFile();
    }
    directory.close();
    if (!found) out.println("[TAPE] no tape images in internal flash; upload one at http://192.168.4.1/");
}

bool mcm_tape_export_text(uint8_t drive, Print &out) {
    if (!mcm_tape_is_mounted(drive)) return false;
    const uint32_t count = g_drives[drive].length;
    for (uint32_t i = 0; i < count; ++i) {
        char token[8];
        const uint16_t v = mcm_tape_read(drive, i);
        snprintf(token, sizeof(token), "%X ", static_cast<unsigned>(v));
        out.print(token);
        if (i >= 2 && v == 0x33 && mcm_tape_read(drive, i - 1) == 0x17 && mcm_tape_read(drive, i - 2) == 0x17) out.print('\n');
    }
    return true;
}

bool mcm_tape_import_begin(const char *filename) {
    if (!g_storeReady || g_import.active) return false;
    char base[16];
    if (!safeBaseName(filename, base)) return false;
    for (uint8_t d = 0; d < 2; ++d) {
        if (g_drives[d].mounted && strcmp(g_drives[d].baseName, base) == 0) return false;
    }
    if (!LittleFS.exists("/tapes") && !LittleFS.mkdir("/tapes")) return false;
    if (LittleFS.exists("/tapes/.upload.tmp")) LittleFS.remove("/tapes/.upload.tmp");
    g_import = ImportState{};
    strcpy(g_import.baseName, base);
    g_import.file = LittleFS.open("/tapes/.upload.tmp", "w+");
    if (!g_import.file) return false;
    uint8_t blankHeader[kHeaderSize] = {};
    if (g_import.file.write(blankHeader, sizeof(blankHeader)) != sizeof(blankHeader)) {
        mcm_tape_import_cancel();
        return false;
    }
    g_import.active = true;
    return true;
}

bool mcm_tape_import_chunk(const uint8_t *data, size_t length) {
    if (!g_import.active || g_import.failed || (!data && length != 0)) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = data[i];
        if (isspace(c)) {
            if (!emitImportedToken()) return false;
        } else if (isxdigit(c) || c == 'x' || c == 'X') {
            if (g_import.tokenLength + 1 >= sizeof(g_import.token)) { g_import.failed = true; return false; }
            g_import.token[g_import.tokenLength++] = static_cast<char>(c);
        } else {
            g_import.failed = true;
            return false;
        }
    }
    return flushImportBuffer();
}

bool mcm_tape_import_finish(void) {
    if (!g_import.active || g_import.failed || !emitImportedToken() || !flushImportBuffer() || g_import.count == 0) {
        mcm_tape_import_cancel();
        return false;
    }
    uint8_t header[kHeaderSize];
    writeHeader(header, g_import.count);
    if (!g_import.file.seek(0, SeekSet) || g_import.file.write(header, sizeof(header)) != sizeof(header)) {
        mcm_tape_import_cancel();
        return false;
    }
    g_import.file.flush();
    g_import.file.close();

    char target[40];
    char backup[40];
    makePath(g_import.baseName, target);
    snprintf(backup, sizeof(backup), "/tapes/.%s.bak", g_import.baseName);
    if (LittleFS.exists(backup)) LittleFS.remove(backup);
    const bool hadOld = LittleFS.exists(target);
    if (hadOld && !LittleFS.rename(target, backup)) { mcm_tape_import_cancel(); return false; }
    if (!LittleFS.rename("/tapes/.upload.tmp", target)) {
        if (hadOld) (void)LittleFS.rename(backup, target);
        mcm_tape_import_cancel();
        return false;
    }
    if (hadOld) LittleFS.remove(backup);
    g_import = ImportState{};
    return true;
}

void mcm_tape_import_cancel(void) {
    if (g_import.file) g_import.file.close();
    if (LittleFS.exists("/tapes/.upload.tmp")) LittleFS.remove("/tapes/.upload.tmp");
    g_import = ImportState{};
}

void moveTape(int id) {
    (void)id; // The original routine selects the active drive using SelectedDevice.
    uint8_t drive;
    if (SelectedDevice == 0xC8) drive = 0;
    else if (SelectedDevice == 0xC9) drive = 1;
    else return;
    if (!mcm_tape_is_mounted(drive)) return;

    TP &meta = (drive == 0) ? tape0_s : tape1_s;
    int h = meta.w_head;
    int s = meta.status;
    const int speed = meta.speed;
    const int length = static_cast<int>(meta.length);

    move_clock = 0;
    switch (speed) {
        case 0x00:
        case 0x10:
            return;
        case 0x13:
            if (inst != 0x57) mcm_tape_write(drive, static_cast<uint32_t>(h), kBlank);
            [[fallthrough]];
        case 0x03:
            ++h;
            break;
        case 0x05:
            --h;
            break;
        case 0x1B:
            if (inst == 0x57) break;
            for (int i = 0; i < 4; ++i) if (h + i >= 0 && h + i < length) mcm_tape_write(drive, static_cast<uint32_t>(h + i), kBlank);
            [[fallthrough]];
        case 0x0B:
            h += 4;
            break;
        case 0x1D:
            for (int i = 0; i < 4; ++i) if (h - i >= delta && h - i < length) mcm_tape_write(drive, static_cast<uint32_t>(h - i), kBlank);
            [[fallthrough]];
        case 0x0D:
            h -= 4;
            break;
        default:
            break;
    }

    if (h < delta || h >= length) {
        if (h < delta) h = delta; else h = length - 1;
        s = (s | 0x49);
    } else {
        if (mcm_tape_read(drive, static_cast<uint32_t>(h - delta)) == kBlank) s = s | 0x01;
        else s = ((s & 0xFE) | 0xC0);
    }
    meta.status = static_cast<BYTE>(s);
    meta.w_head = h;
}
