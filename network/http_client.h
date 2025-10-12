#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

// ========= Includes Essenciais =========
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lwip/err.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "pico/cyw43_arch.h"

// Strutura para armazenar o estado de uma requisição HTTP
typedef struct HTTP_REQUEST_STATE_T {
    char *request;
    char *host;
    uint16_t port;
    struct tcp_pcb *pcb;
    ip_addr_t remote_addr;

    /* campos novos para acumular a resposta */
    char *resp_buf;           // buffer dinâmico para o corpo (após headers)
    size_t resp_buf_len;      // tamanho alocado
    size_t resp_received;     // bytes do body já recebidos
    int content_length;       // -1 se desconhecido
    int headers_parsed;       // 0 = não, 1 = sim

    /* novo: corpo do POST (binário) */
    uint8_t *body;    // cópia do body a ser enviada (binário)
    size_t body_len;
    size_t body_sent;    // quantos bytes do body já foram enviados
    char *content_type; // string do tipo de conteúdo (ex: "audio/wav")
} HTTP_REQUEST_STATE;

extern void http_client_response_handler(const char *body);

// ========= Funções Auxiliares =========
static err_t http_client_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t http_client_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
static void http_client_err_cb(void *arg, err_t err);
static err_t http_client_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void http_client_close(HTTP_REQUEST_STATE *state);

// ========= Protótipos das Funções =========

// Inicia uma requisição HTTP GET.
err_t http_get_request(const char *host, const char *path, uint16_t port);

// Inicia uma requisição HTTP POST.
err_t http_post_request(const char *host, const char *path, uint16_t port, const char *data);

// Incia uma requisição HTTP PATCH.
err_t http_patch_request(const char *host, const char *path, uint16_t port, const char *data);

// public prototypes (no final do header)
err_t http_post_binary(const char *host, const char *path, uint16_t port, const uint8_t *data, size_t data_len, const char *content_type);

#endif // HTTP_CLIENT_H