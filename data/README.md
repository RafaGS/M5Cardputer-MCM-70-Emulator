# MCM/70 Emulator — M6.1: corrección de rutas en HTTP de cintas

**Plataforma:** M5Stack Cardputer original (ESP32-S3FN8, 8 MB de flash).  
**Base:** M5 (CPU + ROM + teclado + Self-Scan + almacenamiento LittleFS).  
**Objetivo de M6:** subir cintas `.tp` desde un navegador, almacenarlas por bloques en LittleFS y gestionar las dos unidades de cinta desde una interfaz web.

## Uso desde el Cardputer

1. Compila y carga el firmware `m6-cardputer` por USB.
2. En el móvil o PC, conéctate al punto de acceso Wi-Fi del Cardputer:
   - **SSID:** `MCM70-Cardputer`
   - **Contraseña:** `mcm70tape`
3. Abre `http://192.168.4.1/` en el navegador. El punto de acceso no proporciona Internet; el dispositivo web está en esa dirección local.
4. Selecciona un archivo `.tp` de texto y pulsa **Subir a LittleFS**. Cuando la subida finalice, la página actualizará la lista.
5. Pulsa **Montar unidad 0** o **Montar unidad 1** para insertar la cinta en la unidad elegida. Para cambiarla, expulsa primero la cinta montada.

El firmware muestra la SSID y la dirección HTTP en la pantalla. Con la compilación de depuración también imprime SSID, contraseña, IP y memoria libre por Serial-C a 115200 baudios.

**Importante:** cambia la contraseña Wi-Fi antes de usar el punto de acceso en un entorno compartido. Se configura con `MCM_HTTP_AP_PASSWORD` en `platformio.ini`.

## Qué incluye

- `src/hal/http_tape_manager.cpp/.h`: punto de acceso Wi-Fi y servidor HTTP integrado.
- Página web sin dependencias externas, optimizada para navegador móvil y escritorio.
- `GET /api/status`: estado del punto de acceso y capacidad del sistema de archivos.
- `GET /api/tapes`: listado, tamaño y unidades montadas.
- `POST /api/tapes/upload`: importación multipart de `.tp` por bloques, con límite de 1,2 MiB.
- `POST /api/tapes/mount`: monta por nombre base en unidad 0 o 1.
- `POST /api/tapes/eject`: vacía la caché y expulsa la unidad.
- `POST /api/tapes/delete`: elimina una cinta solo si no está montada.
- `POST /api/tapes/flush`: fuerza el vaciado de las páginas modificadas.
- La carga se escribe primero en `/tapes/.upload.tmp`. Solo tras validar todos los tokens hexadecimales se convierte en imagen `M70T` y se mueve al nombre final. Una subida incompleta no se presenta como cinta válida.
- Las imágenes se almacenan en flash interna mediante LittleFS; no se utiliza microSD ni se mantiene toda la cinta en SRAM.

La interfaz admite `.tp` de texto hexadecimal. El nombre base permitido tiene hasta 15 caracteres y solo admite letras, números, punto, guion y guion bajo. Los valores admitidos son `0x000`–`0x100`, incluido el centinela del emulador. Se pueden subir archivos de hasta 1,2 MiB; el tamaño típico original mencionado para una cinta (unos 935 KB) queda dentro de ese límite.

## Compilación en Arch Linux

Instala PlatformIO Core si aún no está disponible y copia las ROM originales a `data/` (por restricciones de distribución, no están incluidas en este paquete):

```bash
./tools/copy_roms.sh /ruta/a/MCM70E_v2.1_distr
pio run -e m7-cardputer
pio run -e m7-cardputer -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Ajusta `/dev/ttyACM0` tras consultar `pio device list`. El alias `m5-cardputer` sigue disponible por compatibilidad.

La partición reserva 4 MiB para la aplicación y el resto para LittleFS, sin una segunda ranura OTA. La actualización del firmware se hace por USB. **No ejecutes `uploadfs` después de empezar a almacenar cintas por HTTP**, ya que el volcado del sistema de archivos sustituye el contenido de LittleFS.

## Pruebas realizadas en este entorno

```bash
./tests/run_keyboard_host_test.sh
./tests/run_tape_storage_host_test.sh
python3 ./tests/test_tape_format.py
```

Las tres pruebas host deben terminar con `PASS`. También se ejecutó la prueba host del backend de cintas contra stubs: importación incremental, persistencia compacta, límites entre páginas, escritura diferida, montaje/expulsión y exportación.

**Limitación de validación:** PlatformIO no está instalado en el entorno que generó este paquete, por lo que no he podido compilar el firmware completo ni ejecutar una subida HTTP en el Cardputer físico. M6 es implementación preparada para validación, no una fase declarada como probada en hardware.

## Arquitectura de cintas

- HTTP es solo el transporte de subida, no el almacenamiento.
- LittleFS guarda una imagen compacta `M70T`: cabecera de 12 bytes y entradas `uint16_t` little-endian.
- Cada unidad utiliza una caché de cuatro páginas de 256 valores; las dos unidades añaden unos 4 KiB de datos de caché.
- `mcm_tape_read()` y `mcm_tape_write()` mantienen los índices lógicos que usa el núcleo; `moveTape()` usa el backend de almacenamiento.
- Las páginas modificadas se vacían al expulsar, con `TAPE FLUSH` y periódicamente durante la ejecución. Un corte de alimentación puede perder las escrituras que aún estén en caché.

## Corrección M6.1

Se corrigió la lectura de metadatos en el listado HTTP para reconstruir rutas absolutas (`/tapes/<nombre>.mct`) cuando el iterador LittleFS devuelve solo el nombre base. Así se evita el aviso `open(): demo.mct does not start with /`. Esta corrección afecta al refresco de la lista web, no al acceso de montaje, que ya usa ruta absoluta.

## Pruebas físicas recomendadas

1. Compilar `pio run -e m7-cardputer` y revisar el uso de flash/RAM.
2. Conectar al AP y abrir `http://192.168.4.1/`.
3. Subir una cinta de prueba pequeña; comprobar que aparece en la lista.
4. Probar un archivo con tokens inválidos y confirmar que no se añade ninguna imagen parcial.
5. Montar en la unidad 0, arrancar el emulador y verificar la lectura del programa; expulsar y volver a montar.
6. Repetir en la unidad 1; comprobar rechazo de expulsión vacía, sustitución de cinta montada y eliminación de cinta montada.
7. Probar una cinta próxima a 935 KB y comprobar la estabilidad de la Wi-Fi, la SRAM libre y el funcionamiento posterior de la CPU.

## Estado de las fases

- M1: pantalla, teclado físico y Serial básicos confirmados por el usuario.
- M2: ROM cargadas y PC en avance confirmados por el usuario.
- M3: renderizador Self-Scan validado en el Cardputer.
- M4: teclado confirmado; el usuario introdujo `2+2` y el emulador mostró `4`.
- M5: backend LittleFS e importador por bloques con pruebas host.
- **M6:** interfaz HTTP y operaciones de cinta implementadas; compilación PlatformIO y pruebas en hardware pendientes.


## M7 — Impresora emulada por HTTP

La salida de impresora se captura en un búfer de texto acotado en RAM. Abre `http://192.168.4.1/printer` para verla, o `http://192.168.4.1/api/printer` para descargarla. El botón **Borrar salida** vacía el búfer explícitamente. La salida no persiste tras reiniciar y la representación es textual, no un facsímil completo de los glifos APL. Consulta `docs/M7_PRINTER_OUTPUT.md`.


## M8 — Integración y regresión

El usuario ha confirmado que `BUN` imprime correctamente en el Cardputer y que `/printer` representa los caracteres y los saltos de línea. El documento `docs/M8_STATUS.md` recoge el estado, las pruebas físicas aún pendientes y el criterio de cierre.

Ejecuta la suite de regresión host con:

```bash
./tests/run_m8_host_regression.sh
```

Esta suite no sustituye la compilación PlatformIO ni las pruebas físicas. No cargues el sistema de archivos (`uploadfs`) sobre un dispositivo que contenga cintas que quieras conservar.
