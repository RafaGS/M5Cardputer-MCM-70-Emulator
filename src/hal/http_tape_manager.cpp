#include "http_tape_manager.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "tape_storage.h"
#include "printer_output.h"
#include "../mcm_core/mcm_core.h"

#ifndef MCM_HTTP_AP_SSID
#define MCM_HTTP_AP_SSID "MCM70-Cardputer"
#endif
#ifndef MCM_HTTP_AP_PASSWORD
#define MCM_HTTP_AP_PASSWORD "mcm70tape"
#endif

namespace {
constexpr size_t kMaxUploadBytes = 1200 * 1024;
constexpr char kTapeDir[] = "/tapes";
constexpr char kTapeExt[] = ".mct";
constexpr char kUploadPage[] PROGMEM = R"HTML(
<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MCM/70 · Cintas</title><style>
:root{color-scheme:dark;--bg:#11151c;--panel:#1c2430;--line:#374151;--txt:#edf2f7;--muted:#aeb9c8;--accent:#55d6be;--warn:#ffd166}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:16px system-ui,sans-serif}main{max-width:760px;margin:auto;padding:20px}h1{font-size:1.5rem;margin:0 0 4px}p{color:var(--muted);line-height:1.45}.panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:16px;margin:14px 0}button,.filepick{background:#263647;color:var(--txt);border:1px solid #53657b;padding:9px 12px;border-radius:8px;cursor:pointer;font:inherit}button.primary{background:#17695e;border-color:#55d6be}button.danger{border-color:#a44}button:disabled{opacity:.45;cursor:wait}input{max-width:100%}.status{font-size:.9rem;color:var(--muted)}.tape{padding:12px 0;border-bottom:1px solid var(--line)}.tape:last-child{border-bottom:0}.name{font-weight:650;overflow-wrap:anywhere}.meta{font-size:.85rem;color:var(--muted);margin:4px 0 9px}.actions{display:flex;gap:8px;flex-wrap:wrap}.message{white-space:pre-wrap}.tag{color:var(--accent);font-size:.85rem}small{color:var(--muted)}
</style></head><body><main><h1>MCM/70 — Gestor de cintas</h1><p>Sube cintas de texto <code>.tp</code> por HTTP. Se convierten por bloques y se guardan en la flash interna; no se necesita microSD.</p>
<section class="panel"><h2>Subir cinta</h2><form id="upload"><input type="file" name="tape" accept=".tp,text/plain" required><p><small>Máximo 1,2 MB. Los nombres deben usar letras, números, punto, guion o guion bajo; máximo 15 caracteres sin extensión.</small></p><button class="primary" id="uploadBtn">Subir a LittleFS</button></form><p id="uploadMsg" class="message status" aria-live="polite"></p></section>
<section class="panel"><h2>Unidades</h2><div id="drives" class="status">Consultando...</div></section>
<section class="panel"><div style="display:flex;gap:12px;align-items:center;justify-content:space-between"><h2 style="margin:0">Cintas en el dispositivo</h2><button id="refresh">Actualizar</button></div><div id="capacity" class="status"></div><div id="tapes">Cargando...</div><p id="listMsg" class="message status"></p></section>
<section class="panel"><h2>Impresora MCP-132</h2><p>Consulta o descarga el texto capturado por la impresora emulada.</p><a href="/printer">Abrir salida de impresora</a> · <a href="/api/printer">Descargar .txt</a></section>
<p class="status">Punto de acceso: <b>MCM70-Cardputer</b> · Abrir <b>http://192.168.4.1/</b>. No necesita conexión a Internet.</p>
</main><script>
const $=s=>document.querySelector(s);
async function post(path,data){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});let j={};try{j=await r.json()}catch{}if(!r.ok)throw Error(j.error||('HTTP '+r.status));return j}
function fmt(n){return (n/1024).toLocaleString('es-ES',{maximumFractionDigits:1})+' KiB'}
async function refresh(){try{const r=await fetch('/api/tapes',{cache:'no-store'});const d=await r.json();if(!r.ok)throw Error(d.error||'No se pudo leer la lista');$('#capacity').textContent=`LittleFS: ${fmt(d.used)} usados de ${fmt(d.total)} · ${fmt(d.free)} libres`;
$('#drives').innerHTML=d.drives.map(x=>`Unidad ${x.drive}: ${x.mounted?`<span class="tag">${x.name} montada</span>`:'sin cinta'}${x.mounted?` <button data-eject="${x.drive}">Expulsar</button>`:''}`).join('<br>');
$('#tapes').innerHTML=d.tapes.length?d.tapes.map(t=>`<div class="tape"><div class="name">${t.name}.tp ${t.mounted?'<span class="tag">· montada en unidad '+t.drive+'</span>':''}</div><div class="meta">${fmt(t.bytes)} · ${t.entries.toLocaleString('es-ES')} valores</div><div class="actions">${t.mounted?'':`<button data-mount="0" data-name="${t.name}">Montar unidad 0</button><button data-mount="1" data-name="${t.name}">Montar unidad 1</button>`}<button class="danger" data-delete="${t.name}" ${t.mounted?'disabled':''}>Eliminar</button></div></div>`).join(''):'<p>No hay cintas guardadas todavía.</p>';
$('#listMsg').textContent='';}catch(e){$('#listMsg').textContent='Error: '+e.message}}
$('#tapes').addEventListener('click',async ev=>{const b=ev.target.closest('button');if(!b)return;b.disabled=true;try{if(b.dataset.mount!==undefined){await post('/api/tapes/mount',{drive:b.dataset.mount,name:b.dataset.name})}else if(b.dataset.eject!==undefined){await post('/api/tapes/eject',{drive:b.dataset.eject})}else if(b.dataset.delete){if(!confirm(`¿Eliminar definitivamente ${b.dataset.delete}.tp?`))return;await post('/api/tapes/delete',{name:b.dataset.delete})}await refresh()}catch(e){alert(e.message)}finally{b.disabled=false}});
$('#drives').addEventListener('click',async ev=>{const b=ev.target.closest('button[data-eject]');if(!b)return;b.disabled=true;try{await post('/api/tapes/eject',{drive:b.dataset.eject});await refresh()}catch(e){alert(e.message)}finally{b.disabled=false}});
$('#refresh').onclick=refresh;
$('#upload').addEventListener('submit',async ev=>{ev.preventDefault();const file=$('#upload input[type=file]').files[0];if(!file)return;if(!file.name.toLowerCase().endsWith('.tp')){alert('Selecciona un archivo .tp de texto.');return}if(file.size>1200*1024){alert('La cinta supera el máximo de 1,2 MB.');return}const btn=$('#uploadBtn');btn.disabled=true;$('#uploadMsg').textContent='Subiendo y convirtiendo por bloques…';try{const fd=new FormData();fd.append('tape',file,file.name);const r=await fetch('/api/tapes/upload',{method:'POST',body:fd});const j=await r.json();if(!r.ok)throw Error(j.error||('HTTP '+r.status));$('#uploadMsg').textContent=`Cinta ${j.name}.tp guardada (${j.entries.toLocaleString('es-ES')} valores).`;ev.target.reset();await refresh()}catch(e){$('#uploadMsg').textContent='Error: '+e.message}finally{btn.disabled=false}});
refresh();setInterval(refresh,15000);
</script></body></html>
)HTML";

WebServer g_server(80);
bool g_store_ready = false;
bool g_upload_active = false;
bool g_upload_ok = false;
bool g_upload_failed = false;
size_t g_upload_bytes = 0;
char g_upload_error[96] = "";
char g_upload_name[24] = "";
uint32_t g_upload_entries = 0;

bool endsWithTp(const char *name) {
    if (!name) return false;
    const size_t n = strlen(name);
    if (n < 3) return false;
    return (tolower(static_cast<unsigned char>(name[n - 3])) == '.' &&
            tolower(static_cast<unsigned char>(name[n - 2])) == 't' &&
            tolower(static_cast<unsigned char>(name[n - 1])) == 'p');
}

void sendJsonError(int code, const char *message) {
    String body = "{\"ok\":false,\"error\":\"";
    for (const char *p = message ? message : "error"; *p; ++p) {
        if (*p == '"' || *p == '\\') body += '\\';
        if (static_cast<unsigned char>(*p) >= 0x20) body += *p;
    }
    body += "\"}";
    g_server.send(code, "application/json; charset=utf-8", body);
}

int mountedDriveFor(const char *base) {
    if (!base) return -1;
    for (int d = 0; d < 2; ++d) {
        TP &meta = (d == 0) ? tape0_s : tape1_s;
        if (mcm_tape_is_mounted(static_cast<uint8_t>(d))) {
            char mountedBase[20] = {};
            strncpy(mountedBase, meta.name, sizeof(mountedBase) - 1);
            const size_t n = strlen(mountedBase);
            if (n >= 3 && strcasecmp(mountedBase + n - 3, ".tp") == 0) mountedBase[n - 3] = '\0';
            if (strcmp(mountedBase, base) == 0) return d;
        }
    }
    return -1;
}

void sendRoot() {
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send_P(200, "text/html; charset=utf-8", kUploadPage);
}

constexpr char kPrinterPage[] PROGMEM = R"HTML(
<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>MCM/70 · Impresora</title><style>
body{margin:0;background:#11151c;color:#edf2f7;font:16px system-ui,sans-serif}main{max-width:900px;margin:auto;padding:18px}a,button{color:#edf2f7;background:#263647;border:1px solid #53657b;border-radius:7px;padding:8px 11px;font:inherit}pre{white-space:pre;overflow:auto;background:#1c2430;border:1px solid #374151;border-radius:10px;padding:14px;min-height:45vh;font:14px/1.4 ui-monospace,monospace}.muted{color:#aeb9c8}</style></head><body><main><h1>Impresora MCP-132</h1><p class="muted">Salida de texto capturada por el emulador. La representación textual no reproduce todos los glifos APL originales.</p><p><a href="/">← Gestor de cintas</a> <a href="/api/printer">Descargar .txt</a> <button id="clear">Borrar salida</button></p><p id="status" class="muted">Cargando…</p><pre id="output"></pre><script>
async function refresh(){try{const r=await fetch('/api/printer',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const t=await r.text();const o=document.querySelector('#output');const nearBottom=o.scrollTop+o.clientHeight>=o.scrollHeight-30;o.textContent=t;if(nearBottom)o.scrollTop=o.scrollHeight;document.querySelector('#status').textContent=t.length+' caracteres capturados';}catch(e){document.querySelector('#status').textContent='Error: '+e.message}}
document.querySelector('#clear').onclick=async()=>{if(!confirm('¿Borrar toda la salida de impresora?'))return;const r=await fetch('/api/printer/clear',{method:'POST'});if(!r.ok)alert('No se pudo borrar la salida');await refresh()};refresh();setInterval(refresh,3000);
</script></main></body></html>
)HTML";

void sendPrinterPage() {
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send_P(200, "text/html; charset=utf-8", kPrinterPage);
}

void sendPrinterText() {
    static char output[128 * (68 + 1) + 1];
    const size_t length = mcm_printer_output_copy(output, sizeof(output));
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.sendHeader("Content-Disposition", "attachment; filename=\"mcm70-printer.txt\"");
    g_server.send(200, "text/plain; charset=utf-8", String(output).substring(0, length));
}

void clearPrinterText() {
    mcm_printer_output_clear();
    g_server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

void sendStatus() {
    if (!g_store_ready) { sendJsonError(503, "El almacenamiento LittleFS no está disponible"); return; }
    String body = "{\"ok\":true,\"ip\":\"" + WiFi.softAPIP().toString() + "\",\"total\":" + String(LittleFS.totalBytes()) +
                  ",\"used\":" + String(LittleFS.usedBytes()) + ",\"free\":" + String(LittleFS.totalBytes() - LittleFS.usedBytes()) + "}";
    g_server.send(200, "application/json; charset=utf-8", body);
}

void sendTapeList() {
    if (!g_store_ready) { sendJsonError(503, "El almacenamiento LittleFS no está disponible"); return; }
    String body;
    body.reserve(1600);
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    body = "{\"ok\":true,\"total\":" + String(total) + ",\"used\":" + String(used) + ",\"free\":" + String(total >= used ? total - used : 0) + ",\"drives\":[";
    for (uint8_t d = 0; d < 2; ++d) {
        TP &meta = d == 0 ? tape0_s : tape1_s;
        if (d) body += ',';
        body += "{\"drive\":" + String(d) + ",\"mounted\":" + String(mcm_tape_is_mounted(d) ? "true" : "false") + ",\"name\":\"";
        body += mcm_tape_is_mounted(d) ? meta.name : "";
        body += "\"}";
    }
    body += "],\"tapes\":[";
    bool first = true;
    File directory = LittleFS.open(kTapeDir, "r");
    if (directory && directory.isDirectory()) {
        File item = directory.openNextFile();
        while (item) {
            const char *path = item.name();
            const char *base = path ? strrchr(path, '/') : nullptr;
            base = base ? base + 1 : path;
            const size_t n = base ? strlen(base) : 0;
            if (!item.isDirectory() && n > 4 && base[0] != '.' && strcmp(base + n - 4, kTapeExt) == 0) {
                char name[16] = {};
                const size_t stem = n - 4;
                if (stem > 0 && stem <= 15) {
                    memcpy(name, base, stem);
                    name[stem] = '\0';
                    uint32_t entries = 0;
                    // ESP32 LittleFS directory iterators may return a basename
                    // (for example "demo.mct") from item.name(), not an absolute
                    // filesystem path. Rebuild the path under /tapes before open().
                    char checkPath[64] = {};
                    snprintf(checkPath, sizeof(checkPath), "%s/%s", kTapeDir, base);
                    File check = LittleFS.open(checkPath, "r");
                    if (check && check.size() >= 12) {
                        uint8_t header[12];
                        if (check.read(header, sizeof(header)) == sizeof(header) && memcmp(header, "M70T", 4) == 0) {
                            entries = static_cast<uint32_t>(header[8]) | (static_cast<uint32_t>(header[9]) << 8) |
                                      (static_cast<uint32_t>(header[10]) << 16) | (static_cast<uint32_t>(header[11]) << 24);
                        }
                    }
                    if (check) check.close();
                    const int drive = mountedDriveFor(name);
                    if (!first) body += ',';
                    first = false;
                    body += "{\"name\":\"" + String(name) + "\",\"bytes\":" + String(item.size()) + ",\"entries\":" + String(entries) +
                            ",\"mounted\":" + String(drive >= 0 ? "true" : "false") + ",\"drive\":" + String(drive) + "}";
                }
            }
            item.close();
            item = directory.openNextFile();
        }
    }
    if (directory) directory.close();
    body += "]}";
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send(200, "application/json; charset=utf-8", body);
}

void handleMount() {
    if (!g_store_ready) { sendJsonError(503, "LittleFS no está disponible"); return; }
    const int drive = g_server.arg("drive").toInt();
    String name = g_server.arg("name");
    if (drive < 0 || drive > 1 || name.isEmpty()) { sendJsonError(400, "Unidad o nombre no válido"); return; }
    if (mcm_tape_is_mounted(static_cast<uint8_t>(drive))) { sendJsonError(409, "La unidad ya tiene una cinta; expúlsala primero"); return; }
    if (!mcm_tape_mount(static_cast<uint8_t>(drive), name.c_str())) { sendJsonError(400, "No se pudo montar la cinta; comprueba el nombre y el formato"); return; }
    g_server.send(200, "application/json", "{\"ok\":true}");
}

void handleEject() {
    const int drive = g_server.arg("drive").toInt();
    if (drive < 0 || drive > 1) { sendJsonError(400, "Unidad no válida"); return; }
    if (!mcm_tape_eject(static_cast<uint8_t>(drive))) { sendJsonError(409, "No se pudo expulsar; la unidad puede estar vacía o falló el vaciado a flash"); return; }
    g_server.send(200, "application/json", "{\"ok\":true}");
}

void handleDelete() {
    String name = g_server.arg("name");
    if (name.isEmpty()) { sendJsonError(400, "Falta el nombre de cinta"); return; }
    if (!mcm_tape_delete(name.c_str())) { sendJsonError(409, "No se pudo eliminar: no existe, el nombre no es válido o la cinta está montada"); return; }
    g_server.send(200, "application/json", "{\"ok\":true}");
}

void handleFlush() {
    mcm_tape_flush_all();
    g_server.send(200, "application/json", "{\"ok\":true}");
}

void handleUploadStream() {
    HTTPUpload &upload = g_server.upload();
    switch (upload.status) {
        case UPLOAD_FILE_START: {
            g_upload_active = false;
            g_upload_ok = false;
            g_upload_failed = false;
            g_upload_bytes = 0;
            g_upload_entries = 0;
            g_upload_error[0] = '\0';
            g_upload_name[0] = '\0';
            if (!g_store_ready) { snprintf(g_upload_error, sizeof(g_upload_error), "LittleFS no está disponible"); g_upload_failed = true; break; }
            // Store only the basename; browsers may include a fake local path.
            const char *filename = upload.filename.c_str();
            const char *slash = strrchr(filename, '/');
            const char *backslash = strrchr(filename, '\\');
            if (slash && (!backslash || slash > backslash)) filename = slash + 1;
            else if (backslash) filename = backslash + 1;
            if (!endsWithTp(filename)) { snprintf(g_upload_error, sizeof(g_upload_error), "Solo se aceptan archivos .tp de texto"); g_upload_failed = true; break; }
            strncpy(g_upload_name, filename, sizeof(g_upload_name) - 1);
            g_upload_name[sizeof(g_upload_name) - 1] = '\0';
            if (!mcm_tape_import_begin(g_upload_name)) {
                snprintf(g_upload_error, sizeof(g_upload_error), "Nombre inválido, almacenamiento lleno o cinta montada con ese nombre");
                g_upload_failed = true;
                break;
            }
            g_upload_active = true;
            break;
        }
        case UPLOAD_FILE_WRITE:
            if (!g_upload_active || g_upload_failed) break;
            g_upload_bytes += upload.currentSize;
            if (g_upload_bytes > kMaxUploadBytes) {
                snprintf(g_upload_error, sizeof(g_upload_error), "La cinta supera el límite de 1,2 MB");
                g_upload_failed = true;
                mcm_tape_import_cancel();
                g_upload_active = false;
            } else if (!mcm_tape_import_chunk(upload.buf, upload.currentSize)) {
                snprintf(g_upload_error, sizeof(g_upload_error), "Formato .tp inválido o error escribiendo en LittleFS");
                g_upload_failed = true;
                mcm_tape_import_cancel();
                g_upload_active = false;
            }
            break;
        case UPLOAD_FILE_END:
            if (g_upload_active && !g_upload_failed) {
                g_upload_ok = mcm_tape_import_finish();
                g_upload_active = false;
                if (!g_upload_ok) {
                    snprintf(g_upload_error, sizeof(g_upload_error), "Cinta incompleta, formato no válido o error de flash");
                    g_upload_failed = true;
                } else {
                    // Extract the compact image's entry count for the success response.
                    char base[16] = {};
                    const char *fn = g_upload_name;
                    const char *slash = strrchr(fn, '/');
                    if (slash) fn = slash + 1;
                    strncpy(base, fn, sizeof(base) - 1);
                    const size_t n = strlen(base);
                    if (n >= 3 && strcasecmp(base + n - 3, ".tp") == 0) base[n - 3] = '\0';
                    char path[40];
                    snprintf(path, sizeof(path), "/tapes/%s.mct", base);
                    File f = LittleFS.open(path, "r");
                    if (f && f.size() >= 12) {
                        uint8_t hdr[12];
                        if (f.read(hdr, sizeof(hdr)) == sizeof(hdr)) g_upload_entries = static_cast<uint32_t>(hdr[8]) | (static_cast<uint32_t>(hdr[9]) << 8) | (static_cast<uint32_t>(hdr[10]) << 16) | (static_cast<uint32_t>(hdr[11]) << 24);
                    }
                    if (f) f.close();
                }
            }
            break;
        case UPLOAD_FILE_ABORTED:
            if (g_upload_active) mcm_tape_import_cancel();
            g_upload_active = false;
            g_upload_ok = false;
            g_upload_failed = true;
            snprintf(g_upload_error, sizeof(g_upload_error), "Subida cancelada por el cliente");
            break;
        default:
            break;
    }
}

void handleUploadResult() {
    if (g_upload_ok && !g_upload_failed) {
        String name = g_upload_name;
        const size_t n = name.length();
        if (n >= 3 && name.substring(n - 3).equalsIgnoreCase(".tp")) name.remove(n - 3);
        String body = "{\"ok\":true,\"name\":\"" + name + "\",\"entries\":" + String(g_upload_entries) + ",\"bytes\":" + String(g_upload_bytes) + "}";
        g_server.send(201, "application/json; charset=utf-8", body);
    } else {
        sendJsonError(400, g_upload_error[0] ? g_upload_error : "Falló la subida; revisa el archivo .tp");
    }
    g_upload_active = false;
    g_upload_ok = false;
    g_upload_failed = false;
    g_upload_error[0] = '\0';
}

void handleNotFound() { sendJsonError(404, "Ruta HTTP no encontrada"); }
} // namespace

bool mcm_http_tape_manager_begin(bool tape_store_ready) {
    g_store_ready = tape_store_ready;
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    const IPAddress localIp(192, 168, 4, 1);
    const IPAddress gateway(192, 168, 4, 1);
    const IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(localIp, gateway, subnet);
    const bool apStarted = WiFi.softAP(MCM_HTTP_AP_SSID, MCM_HTTP_AP_PASSWORD);

    g_server.on("/", HTTP_GET, sendRoot);
    g_server.on("/printer", HTTP_GET, sendPrinterPage);
    g_server.on("/api/printer", HTTP_GET, sendPrinterText);
    g_server.on("/api/printer/clear", HTTP_POST, clearPrinterText);
    g_server.on("/api/status", HTTP_GET, sendStatus);
    g_server.on("/api/tapes", HTTP_GET, sendTapeList);
    g_server.on("/api/tapes/mount", HTTP_POST, handleMount);
    g_server.on("/api/tapes/eject", HTTP_POST, handleEject);
    g_server.on("/api/tapes/delete", HTTP_POST, handleDelete);
    g_server.on("/api/tapes/flush", HTTP_POST, handleFlush);
    g_server.on("/api/tapes/upload", HTTP_POST, handleUploadResult, handleUploadStream);
    g_server.onNotFound(handleNotFound);
    g_server.begin();

#if !defined(MCM_DEBUG_SERIAL) || MCM_DEBUG_SERIAL
    Serial.printf("[HTTP] AP %s: %s\r\n", MCM_HTTP_AP_SSID, apStarted ? "started" : "FAILED");
    Serial.printf("[HTTP] AP password: %s\r\n", MCM_HTTP_AP_PASSWORD);
    Serial.printf("[HTTP] Open http://%s/\r\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[HTTP] LittleFS tape store: %s\r\n", g_store_ready ? "ready" : "unavailable");
#endif
    return apStarted;
}

void mcm_http_tape_manager_handle_client() {
    g_server.handleClient();
}
