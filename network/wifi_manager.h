#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// ========= Includes Essenciais =========
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "wifi_credentials.h"

// ========= Definições de Constantes =========
#define WIFI_CONNECT_TIMEOUT_MS 30000

// ========= Protótipos das Funções =========
bool wifi_arch_init(void);
bool wifi_connect(void);
bool wifi_is_connected(void);
void wifi_print_status(void);

#endif // WIFI_MANAGER_H