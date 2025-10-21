#include "http_client.h"
#include <ctype.h>

#ifndef HAVE_STRCASESTR
static char *strcasestr_fallback(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            ++h;
            ++n;
        }
        if (!*n)
            return (char *)haystack;
    }
    return NULL;
}
#define strcasestr strcasestr_fallback
#endif

// Função para fechar a conexão e liberar recursos
static void http_client_close(HTTP_REQUEST_STATE *state) {
    if (!state) return;

    if (state->pcb) {
        tcp_arg(state->pcb, NULL);
        tcp_poll(state->pcb, NULL, 0);
        tcp_sent(state->pcb, NULL);
        tcp_recv(state->pcb, NULL);
        tcp_err(state->pcb, NULL);
        tcp_close(state->pcb);
        state->pcb = NULL;
    }

    if (state->request) {
        free(state->request);
        state->request = NULL;
    }

    if (state->host) {
        free(state->host);
        state->host = NULL;
    }

    if (state->resp_buf) {
        free(state->resp_buf);
        state->resp_buf = NULL;
    }

    /* liberar o body do POST (binário) se alocado */
    if (state->body) {
        free(state->body);
        state->body = NULL;
        state->body_len = 0;
        state->body_sent = 0;
    }

    /* liberar content_type se foi strdup'ed */
    if (state->content_type) {
        free(state->content_type);
        state->content_type = NULL;
    }

    free(state);
}

// tenta enviar bytes do body enquanto houver espaço no send buffer.
// retorna ERR_OK em condições normais, ou outro err_t em erro fatal.
static err_t try_send_body(struct tcp_pcb *tpcb, HTTP_REQUEST_STATE *state) {
    if (!state || !tpcb || !state->body || state->body_sent >= state->body_len) return ERR_OK;

    while (state->body_sent < state->body_len) {
        u16_t avail = tcp_sndbuf(tpcb);                // espaço disponível no sndbuf
        if (avail == 0) break;                         // nada a enviar agora
        // limita por mtu razoável e por sndbuf
        size_t remaining = state->body_len - state->body_sent;
        size_t to_send = remaining;
        if (to_send > avail) to_send = avail;
        if (to_send > 1024) to_send = 1024; // envia em chunks de até 1KiB

        cyw43_arch_lwip_begin();
        err_t err = tcp_write(tpcb, state->body + state->body_sent, (u16_t)to_send, TCP_WRITE_FLAG_COPY);
        cyw43_arch_lwip_end();

        if (err == ERR_OK) {
            state->body_sent += to_send;
            // continue loop e tente enviar mais (até esgotar sndbuf)
        } else if (err == ERR_MEM) {
            // send buffer não comportou esse bloco agora -> saia e aguarde tcp_sent
            break;
        } else {
            // erro fatal
            return err;
        }
    }
    return ERR_OK;
}

// Callback: Dados recebidos do servidor
static err_t http_client_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    if (!p) {
        // conexão encerrada pelo servidor => se ainda temos dados acumulados e não processados, processa-os
        if (state && state->resp_received > 0) {
            // garante terminação da string
            if (state->resp_buf && state->resp_received < state->resp_buf_len) {
                state->resp_buf[state->resp_received] = '\0';
            } else if (state->resp_buf) {
                // realoca para garantir espaço para '\0'
                state->resp_buf = realloc(state->resp_buf, state->resp_received + 1);
                if (state->resp_buf) state->resp_buf[state->resp_received] = '\0';
            }
            extern void http_client_response_handler(const char *body);
            if (state->resp_buf) http_client_response_handler(state->resp_buf);
        }
        http_client_close(state);
        return ERR_OK;
    }

    // calcula total recebido neste callback
    int total_len = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next) total_len += q->len;

    // monta um buffer temporário com todos os bytes recebidos neste callback
    char *tmp = malloc(total_len);
    if (!tmp) {
        pbuf_free(p);
        http_client_close(state);
        return ERR_MEM;
    }
    int off = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        memcpy(tmp + off, q->payload, q->len);
        off += q->len;
    }

    // Se os headers ainda não foram parseados, tentamos localizar o fim dos headers
    if (!state->headers_parsed) {
        // busca o separador CRLF CRLF
        char *sep = NULL;
        // precisamos garantir string temporária com +1 para usar strstr com segurança
        char *tmp_nul = malloc(total_len + 1);
        if (!tmp_nul) {
            free(tmp);
            pbuf_free(p);
            http_client_close(state);
            return ERR_MEM;
        }
        memcpy(tmp_nul, tmp, total_len);
        tmp_nul[total_len] = '\0';
        sep = strstr(tmp_nul, "\r\n\r\n");

        if (sep) {
            // headers presentes neste chunk: calcula offset do body dentro deste tmp
            int header_end_offset = (int)(sep - tmp_nul) + 4;
            int body_part_len = total_len - header_end_offset;
            // tenta ler Content-Length do bloco de headers
            tmp_nul[header_end_offset - 4] = '\0'; // cortar para ficar só headers temporariamente
            char *cl = strcasestr(tmp_nul, "Content-Length:");
            if (cl) {
                cl += strlen("Content-Length:");
                while (*cl == ' ') cl++;
                state->content_length = atoi(cl);
            } else {
                state->content_length = -1; // desconhecido
            }
            state->headers_parsed = 1;

            // se houver body neste mesmo chunk, copia-o para resp_buf
            if (body_part_len > 0) {
                size_t want = (state->content_length > 0) ? (size_t)state->content_length : (size_t)body_part_len;
                // aloca (ou realoca) resp_buf para content_length se conhecido, senão para body_part_len inicialmente
                if (state->content_length > 0) {
                    state->resp_buf = malloc((size_t)state->content_length + 1);
                    state->resp_buf_len = (size_t)state->content_length + 1;
                } else {
                    state->resp_buf = malloc((size_t)body_part_len + 1);
                    state->resp_buf_len = (size_t)body_part_len + 1;
                }
                if (!state->resp_buf) {
                    free(tmp_nul);
                    free(tmp);
                    pbuf_free(p);
                    http_client_close(state);
                    return ERR_MEM;
                }
                memcpy(state->resp_buf, tmp + header_end_offset, body_part_len);
                state->resp_received = (size_t)body_part_len;
            }
        }
        free(tmp_nul);
    } else {
        // headers já parseados: todo esse tmp é parte do body
        if (total_len > 0) {
            // garante espaço
            size_t need = state->resp_received + (size_t)total_len + 1;
            if (need > state->resp_buf_len) {
                size_t new_len = (state->content_length > 0) ? (size_t)state->content_length + 1 : need;
                char *r = realloc(state->resp_buf, new_len);
                if (!r) {
                    free(tmp);
                    pbuf_free(p);
                    http_client_close(state);
                    return ERR_MEM;
                }
                state->resp_buf = r;
                state->resp_buf_len = new_len;
            }
            memcpy(state->resp_buf + state->resp_received, tmp, total_len);
            state->resp_received += (size_t)total_len;
        }
    }

    // Se já conhecemos Content-Length e já recebemos tudo -> chamar handler
    if (state->headers_parsed && state->content_length > 0 && state->resp_received >= (size_t)state->content_length) {
        // terminação
        if (state->resp_buf) state->resp_buf[state->resp_received] = '\0';
        extern void http_client_response_handler(const char *body);
        if (state->resp_buf) http_client_response_handler(state->resp_buf);

        // Consumimos os bytes - informa lwIP
        tcp_recved(state->pcb, (u16_t) total_len);

        free(tmp);
        pbuf_free(p);
        // fechamos e liberamos o estado
        http_client_close(state);
        return ERR_OK;
    }

    // Se não conhecemos Content-Length, aguardamos conexão fechar para então processar (handled when p==NULL above)

    // informa ao stack que consumimos os bytes
    tcp_recved(state->pcb, (u16_t) total_len);

    free(tmp);
    pbuf_free(p);
    return ERR_OK;
}

// Callback: Conexão estabelecida
static err_t http_client_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    if (err != ERR_OK) {
        printf("Falha ao conectar: %d\n", err);
        http_client_close(state);
        return err;
    }

    printf("Conectado a %s. Enviando requisição...\n", ipaddr_ntoa(&state->remote_addr));
    cyw43_arch_lwip_begin();
    err = tcp_write(tpcb, state->request, strlen(state->request), TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("Falha ao escrever dados (headers): %d\n", err);
        http_client_close(state);
        return err;
    }

    // já enviou state->request (headers)
    // agora tente enviar o body em blocos (se houver)
    if (state->body && state->body_len > 0) {
        err_t err2 = try_send_body(tpcb, state);
        if (err2 != ERR_OK) {
            printf("Falha ao enviar body inicial: %d\n", err2);
            http_client_close(state);
            return err2;
        }
        // se todo body foi enviado aqui mesmo, ótimo — o recv_cb tratará resposta.
        // se não, o restante será enviado no callback http_client_sent_cb.
    }

    return ERR_OK;
}

// Callback: Erro na conexão
static void http_client_err_cb(void *arg, err_t err) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    printf("Erro de TCP: %d\n", err);
    http_client_close(state);
}

// Callback: Dados enviados com sucesso
static err_t http_client_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)arg;
    if (!state) return ERR_OK;

    // se há body pendente, tente enviar o que restou
    if (state->body && state->body_sent < state->body_len) {
        err_t r = try_send_body(tpcb, state);
        if (r != ERR_OK) {
            printf("Erro em try_send_body no tcp_sent: %d\n", r);
            http_client_close(state);
            return r;
        }
    }
    return ERR_OK;
}

// Callback: Resolução de DNS concluída
static void http_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    HTTP_REQUEST_STATE *state = (HTTP_REQUEST_STATE *)callback_arg;
    if (ipaddr) {
        state->remote_addr = *ipaddr;
        printf("DNS resolvido: %s -> %s\n", name, ipaddr_ntoa(ipaddr));

        cyw43_arch_lwip_begin();
        state->pcb = tcp_new();
        if (!state->pcb) {
            printf("tcp_new() returned NULL - no TCP PCB available\n");
            http_client_close(state);
            return;
        }
        tcp_arg(state->pcb, state);
        tcp_recv(state->pcb, http_client_recv_cb);
        tcp_err(state->pcb, http_client_err_cb);
        tcp_sent(state->pcb, http_client_sent_cb);
        err_t err = tcp_connect(state->pcb, &state->remote_addr, state->port, http_client_connected_cb);
        cyw43_arch_lwip_end();

        if (err != ERR_OK) {
            printf("Falha em tcp_connect: %d\n", err);
            http_client_close(state);
        }
    } else {
        printf("Falha na requisição DNS\n");
        http_client_close(state);
    }
}

// Função auxiliar para iniciar uma requisição
static err_t start_http_request(const char *host, const char *path, uint16_t port, const char *method, const char* data) {
    HTTP_REQUEST_STATE *state = calloc(1, sizeof(HTTP_REQUEST_STATE));
    if (!state) return ERR_MEM;

    char *request_template;
    int request_len;
    if (data) {
        request_template = "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s";
        request_len = snprintf(NULL, 0, request_template, method, path, host, strlen(data), data);
    } else {
        request_template = "%s %s HTTP/1.1\r\nHost: %s\r\n\r\n";
        request_len = snprintf(NULL, 0, request_template, method, path, host);
    }

    state->request = malloc(request_len + 1);
    if (!state->request) {
        free(state);
        return ERR_MEM;
    }

    if(data) {
        sprintf(state->request, request_template, method, path, host, strlen(data), data);
    } else {
        sprintf(state->request, request_template, method, path, host);
    }

    state->host = strdup(host);
    state->port = port;

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(host, &state->remote_addr, http_dns_found_cb, state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) { // IP já está no cache
        http_dns_found_cb(host, &state->remote_addr, state);
    } else if (err != ERR_INPROGRESS) {
        printf("Falha ao iniciar requisição DNS\n");
        http_client_close(state);
        return err;
    }

    return ERR_OK;
}

// --- função auxiliar para enviar requisição com body binário ---
static err_t start_http_request_binary(const char *host, const char *path, uint16_t port, const uint8_t *body, size_t body_len, const char *method, const char *content_type) {
    HTTP_REQUEST_STATE *state = calloc(1, sizeof(HTTP_REQUEST_STATE));
    if (!state) return ERR_MEM;

    // header template com Content-Type e Content-Length
    const char *request_template = "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n";
    int header_len = snprintf(NULL, 0, request_template, method, path, host, content_type ? content_type : "application/octet-stream", (int)body_len);
    state->request = malloc(header_len + 1);
    if (!state->request) {
        free(state);
        return ERR_MEM;
    }
    sprintf(state->request, request_template, method, path, host, content_type ? content_type : "application/octet-stream", (int)body_len);

    // copia host
    state->host = strdup(host);
    state->port = port;

    // copia body para o state (persistente até fechar)
    if (body && body_len > 0) {
        state->body = malloc(body_len);
        if (!state->body) {
            free(state->request);
            free(state->host);
            free(state);
            return ERR_MEM;
        }
        memcpy(state->body, body, body_len);
        state->body_len = body_len;
    } else {
        state->body = NULL;
        state->body_len = 0;
    }

    // copia content_type
    if (content_type) state->content_type = strdup(content_type);

    // inicia DNS/connect
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(host, &state->remote_addr, http_dns_found_cb, state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        http_dns_found_cb(host, &state->remote_addr, state);
    } else if (err != ERR_INPROGRESS) {
        http_client_close(state);
        return err;
    }

    return ERR_OK;
}

err_t start_http_request_binary_take_ownership(const char *host, const char *path, uint16_t port, uint8_t *body, size_t body_len, const char *method, const char *content_type) {
    HTTP_REQUEST_STATE *state = calloc(1, sizeof(HTTP_REQUEST_STATE));
    if (!state) return ERR_MEM;

    const char *request_template = "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n";
    int header_len = snprintf(NULL, 0, request_template, method, path, host, content_type ? content_type : "application/octet-stream", (int)body_len);
    state->request = malloc(header_len + 1);
    if (!state->request) {
        free(state);
        return ERR_MEM;
    }
    sprintf(state->request, request_template, method, path, host, content_type ? content_type : "application/octet-stream", (int)body_len);

    state->host = strdup(host);
    state->port = port;

    // aqui em vez de copiar, assumimos ownership do 'body' passado
    state->body = body;
    state->body_len = body_len;
    state->body_sent = 0;

    if (content_type) state->content_type = strdup(content_type);

    // DNS / connect
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(host, &state->remote_addr, http_dns_found_cb, state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        http_dns_found_cb(host, &state->remote_addr, state);
    } else if (err != ERR_INPROGRESS) {
        http_client_close(state);
        return err;
    }
    return ERR_OK;
}

err_t http_post_binary_take_ownership(const char *host, const char *path, uint16_t port, uint8_t *data, size_t data_len, const char *content_type) {
    return start_http_request_binary_take_ownership(host, path, port, data, data_len, "POST", content_type);
}

// wrapper público
err_t http_post_binary(const char *host, const char *path, uint16_t port, const uint8_t *data, size_t data_len, const char *content_type) {
    return start_http_request_binary(host, path, port, data, data_len, "POST", content_type);
}

// Implementação das funções públicas
err_t http_get_request(const char *host, const char *path, uint16_t port) {
    return start_http_request(host, path, port, "GET", NULL);
}

err_t http_post_request(const char *host, const char *path, uint16_t port, const char *data) {
    return start_http_request(host, path, port, "POST", data);
}

err_t http_patch_request(const char *host, const char *path, uint16_t port, const char *data) {
    return start_http_request(host, path, port, "PATCH", data);
}