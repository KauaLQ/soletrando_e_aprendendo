#include "http_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Definição completa da estrutura de estado, visível apenas neste arquivo.
struct HTTP_REQUEST_STATE_T {
    struct tcp_pcb *pcb;
    ip_addr_t remote_addr;
    char *host;
    uint16_t port;

    char *request;
    size_t request_len;
    size_t send_pos;
    bool sending;

    uint8_t *resp;
    size_t resp_len;
    size_t resp_cap;

    http_response_cb_t response_cb;
    void *user_ctx;
};

// --- Protótipos de funções estáticas ---
static void http_client_free_state(HTTP_REQUEST_STATE *state);
static err_t http_client_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t http_client_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
static void http_client_err_cb(void *arg, err_t err);
static err_t http_client_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void http_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg);

/**
 * @brief Função interna para fechar a conexão e liberar TODOS os recursos associados a um estado.
 * Esta é a única função que deve fazer a limpeza.
 */
static void http_client_free_state(HTTP_REQUEST_STATE *state) {
    if (!state) {
        return;
    }

    if (state->pcb) {
        // Desassocia o argumento para evitar chamadas de callback em um ponteiro inválido.
        tcp_arg(state->pcb, NULL);
        tcp_poll(state->pcb, NULL, 0);
        tcp_sent(state->pcb, NULL);
        tcp_recv(state->pcb, NULL);
        tcp_err(state->pcb, NULL);
        // O fechamento pode falhar se a memória estiver baixa, mas tentamos mesmo assim.
        tcp_close(state->pcb);
        state->pcb = NULL;
    }

    if (state->request) free(state->request);
    if (state->host) free(state->host);
    if (state->resp) free(state->resp);
    
    // Libera a própria estrutura de estado.
    free(state);
}

// Implementação da nova API pública para abortar
void http_client_abort_request(HTTP_REQUEST_STATE *state) {
    if (state) {
        printf("Abortando requisicao HTTP e limpando recursos.\n");
        http_client_free_state(state);
    }
}

// --- Implementação dos Callbacks (semelhante ao anterior) ---

static err_t http_client_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    if (!state) {
        if (p) pbuf_free(p);
        return ERR_ABRT;
    }

    if (err != ERR_OK) {
        printf("recv_cb: erro %d\n", err);
        http_client_free_state(state);
        return err;
    }

    if (p == NULL) { // Conexão fechada pelo servidor
        printf("Conexao encerrada pelo servidor.\n");
        if (state->response_cb && state->resp && state->resp_len > 0) {
            state->response_cb((const char *)state->resp, state->resp_len, state->user_ctx);
        }
        http_client_free_state(state); // Limpa tudo
        return ERR_OK;
    }

    if (p->len > 0) {
        if (state->resp_cap < state->resp_len + p->len + 1) {
            state->resp_cap = state->resp_len + p->len + 256;
            state->resp = realloc(state->resp, state->resp_cap);
            if (!state->resp) {
                printf("Falha ao realocar buffer de resposta\n");
                pbuf_free(p);
                http_client_free_state(state);
                return ERR_MEM;
            }
        }
        memcpy(state->resp + state->resp_len, p->payload, p->len);
        state->resp_len += p->len;
        state->resp[state->resp_len] = '\0';
    }

    tcp_recved(tpcb, p->len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t http_client_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    if (err != ERR_OK) {
        printf("Falha ao conectar: %d\n", err);
        http_client_free_state(state);
        return err;
    }

    printf("Conectado a %s. Enviando requisicao... (bytes=%zu)\n", ipaddr_ntoa(&state->remote_addr), state->request_len);

    state->send_pos = 0;
    state->sending = true;

    // Chama o callback de envio para iniciar a transmissão
    return http_client_sent_cb(arg, tpcb, 0);
}

static void http_client_err_cb(void *arg, err_t err) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    printf("Erro de TCP: %d\n", err);
    http_client_free_state(state); // Limpa o estado em caso de erro
}

static err_t http_client_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    if (!state) return ERR_ABRT;
    if (!state->sending) return ERR_OK;

    if (state->send_pos < state->request_len) {
        cyw43_arch_lwip_begin();
        u16_t snd_buf = tcp_sndbuf(tpcb);
        cyw43_arch_lwip_end();

        if (snd_buf == 0) return ERR_OK;

        size_t remaining = state->request_len - state->send_pos;
        size_t to_send = remaining > snd_buf ? snd_buf : remaining;

        cyw43_arch_lwip_begin();
        err_t werr = tcp_write(tpcb, state->request + state->send_pos, (u16_t)to_send, TCP_WRITE_FLAG_COPY);
        cyw43_arch_lwip_end();

        if (werr == ERR_OK) {
            state->send_pos += to_send;
            cyw43_arch_lwip_begin();
            tcp_output(tpcb);
            cyw43_arch_lwip_end();
        } else {
            printf("tcp_sent_cb: tcp_write falhou %d\n", werr);
        }
    } else {
        state->sending = false; // Todos os dados foram enviados
    }

    return ERR_OK;
}

static void http_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)callback_arg;
    if (ipaddr) {
        state->remote_addr = *ipaddr;
        printf("DNS resolvido: %s -> %s\n", name, ipaddr_ntoa(ipaddr));

        cyw43_arch_lwip_begin();
        state->pcb = tcp_new();
        tcp_arg(state->pcb, state);
        tcp_recv(state->pcb, http_client_recv_cb);
        tcp_err(state->pcb, http_client_err_cb);
        tcp_sent(state->pcb, http_client_sent_cb);
        err_t err = tcp_connect(state->pcb, &state->remote_addr, state->port, http_client_connected_cb);
        cyw43_arch_lwip_end();

        if (err != ERR_OK) {
            printf("Falha em tcp_connect: %d\n", err);
            http_client_free_state(state);
            // Neste ponto não podemos retornar um erro, pois estamos em um callback.
            // A limpeza do estado é o que podemos fazer.
        }
    } else {
        printf("Falha na requisicao DNS\n");
        http_client_free_state(state);
    }
}

// --- Funções principais de requisição ---

// Em http_client.c

static HTTP_REQUEST_STATE* start_http_request_binary_ex(const char *host, const char *path, uint16_t port,
                                                        const char *method,
                                                        const char *content_type,
                                                        const uint8_t *body, size_t body_len,
                                                        http_response_cb_t cb, void *user_ctx) {
    HTTP_REQUEST_STATE *state = calloc(1, sizeof(HTTP_REQUEST_STATE));
    if (!state) {
        printf("Falha ao alocar estado\n");
        return NULL;
    }

    // CORREÇÃO: Adicionamos o "x-api-key" de volta nos modelos
    const char *header_template;
    if (strcmp(method, "GET") == 0) {
        header_template = "%s %s HTTP/1.1\r\nHost: %s\r\nx-api-key: minha_chave_segura\r\n\r\n";
    } else {
        header_template = "%s %s HTTP/1.1\r\nHost: %s\r\nx-api-key: minha_chave_segura\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n";
    }
    
    int header_len = snprintf(NULL, 0, header_template, method, path, host, content_type, body_len);
    if (header_len < 0) {
        http_client_free_state(state);
        return NULL;
    }

    size_t total_len = (size_t)header_len + body_len;
    state->request = malloc(total_len + 1);
    if (!state->request) {
        http_client_free_state(state);
        return NULL;
    }

    int written;
    if (strcmp(method, "GET") == 0) {
        written = sprintf(state->request, header_template, method, path, host);
    } else {
        written = sprintf(state->request, header_template, method, path, host, content_type, body_len);
    }
    
    if (body_len > 0) {
        memcpy((uint8_t *)state->request + written, body, body_len);
    }

    state->request_len = total_len;
    state->host = strdup(host);
    state->port = port;
    state->response_cb = cb;
    state->user_ctx = user_ctx;

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(host, &state->remote_addr, http_dns_found_cb, state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) { // IP já no cache
        http_dns_found_cb(host, &state->remote_addr, state);
    } else if (err != ERR_INPROGRESS) { // Erro real
        printf("Falha ao iniciar requisicao DNS: %d\n", err);
        http_client_free_state(state);
        return NULL;
    }
    
    return state;
}

// Implementação das funções públicas
HTTP_REQUEST_STATE* http_get_request_cb(const char *host, const char *path, uint16_t port,
                                        http_response_cb_t cb, void *user_ctx) {
    return start_http_request_binary_ex(host, path, port, "GET", NULL, NULL, 0, cb, user_ctx);
}

HTTP_REQUEST_STATE* http_post_binary_request_cb(const char *host, const char *path, uint16_t port,
                                                const char *content_type,
                                                const uint8_t *data_ptr, size_t data_len,
                                                http_response_cb_t cb, void *user_ctx) {
    return start_http_request_binary_ex(host, path, port, "POST", content_type, data_ptr, data_len, cb, user_ctx);
}


// --- Implementação das Funções Legadas (não devem ser usadas para novas lógicas) ---
err_t http_get_request(const char *host, const char *path, uint16_t port) {
    HTTP_REQUEST_STATE* state = http_get_request_cb(host, path, port, NULL, NULL);
    return state ? ERR_OK : ERR_MEM;
}

err_t http_post_binary_request(const char *host, const char *path, uint16_t port,
                               const char *content_type,
                               const uint8_t *data_ptr, size_t data_len) {
    HTTP_REQUEST_STATE* state = http_post_binary_request_cb(host, path, port, content_type, data_ptr, data_len, NULL, NULL);
    return state ? ERR_OK : ERR_MEM;
}

// ... implementações restantes de funções legadas se necessário ...
err_t http_post_request(const char *host, const char *path, uint16_t port, const char *data) {
     HTTP_REQUEST_STATE* state = http_post_binary_request_cb(host, path, port, "application/json", (const uint8_t*)data, strlen(data), NULL, NULL);
     return state ? ERR_OK : ERR_MEM;
}

err_t http_patch_request(const char *host, const char *path, uint16_t port, const char *data) {
    // Nota: precisa de uma implementação em start_http_request_binary_ex para "PATCH"
    // Esta é apenas uma aproximação.
    HTTP_REQUEST_STATE* state = start_http_request_binary_ex(host, path, port, "PATCH", "application/json", (const uint8_t*)data, strlen(data), NULL, NULL);
    return state ? ERR_OK : ERR_MEM;
}