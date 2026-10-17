#pragma once

#include <stdbool.h>

// Starts a device-hosted Wi-Fi access point and the local HTTP tape manager.
// Connect a phone/computer to the AP and open http://192.168.4.1/.
bool mcm_http_tape_manager_begin(bool tape_store_ready);
void mcm_http_tape_manager_handle_client();
