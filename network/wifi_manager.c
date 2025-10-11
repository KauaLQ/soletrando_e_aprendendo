#include <stdio.h>
#include "hardware/watchdog.h"
#include "wifi_manager.h"
#include "wifi_credentials.h"
#include "lwip/netif.h"

// Função para inicializar o hardware do Wi-Fi 
bool wifi_arch_init(void) {
	// Inicializa o chip wi-fi utilizando a biblioteca cyw43_arch
    if (cyw43_arch_init()) {
        printf("Falha ao inicializar o módulo Wi-Fi (cyw43_arch_init).\n");
        return false;
    }
    return true;
}

// Função para conectar à rede Wi-Fi
bool wifi_connect(void) {
	// 1. Habilita o modo Station (cliente Wi-Fi)
    cyw43_arch_enable_sta_mode(); 
    printf("Conectando à rede '%s'...\n", WIFI_SSID);

    bool wf_flag = true;

    while (wf_flag)
    {
        // 2. Tenta conectar à rede com timeout
        // O último argumento define o timeout em milissegundos (30s)
        if (!cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, WIFI_CONNECT_TIMEOUT_MS)) {
            printf("Conectado com sucesso!\n");
            printf("IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
            return true;
            return wf_flag = false;
        }
        else{
            printf("Falha ao conectar ao Wi-Fi.\n");
            // watchdog_enable(delay_ms, pause_on_debug)
            // delay_ms: Tempo em milissegundos antes da reinicialização. O mínimo é 1.
            // pause_on_debug: Define se o watchdog deve pausar quando um depurador está conectado.
            //                 Geralmente TRUE é usado para desenvolvimento.
            watchdog_enable(1, true); 

            // Loop de espera ocupada (busy-wait)
            // O programa ficará preso aqui por 1ms até o watchdog resetar a placa.
            while(1);
            // O código após o while(1) NUNCA será alcançado.
        }
    }
}

// Função para verificar se está conectado ao Wi-Fi
bool wifi_is_connected(void) {
    return cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

// Função para obter informações da conexão Wi-Fi
void wifi_print_status(void) {
    if (wifi_is_connected()) {
        printf("Wi-Fi: Conectado\n");
        printf("IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
        printf("Gateway: %s\n", ip4addr_ntoa(netif_ip4_gw(netif_default)));
        printf("Netmask: %s\n", ip4addr_ntoa(netif_ip4_netmask(netif_default)));
    } else {
        printf("Wi-Fi: Desconectado\n");
    }
}