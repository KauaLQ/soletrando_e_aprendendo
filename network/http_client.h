#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/err.h"

// Define o tipo HTTP_REQUEST_STATE como uma estrutura incompleta (ponteiro opaco).
// A definição completa da estrutura ficará apenas no arquivo .c.
typedef struct HTTP_REQUEST_STATE_T HTTP_REQUEST_STATE;

// Definição do tipo de função para o callback de resposta.
typedef void (*http_response_cb_t)(const char *body, size_t len, void *user_ctx);

/**
 * @brief Inicia uma requisição GET assíncrona.
 * * @param host O hostname ou IP do servidor.
 * @param path O caminho do recurso (ex: "/index.html").
 * @param port A porta do servidor.
 * @param cb A função de callback a ser chamada com a resposta.
 * @param user_ctx Um ponteiro de usuário para ser passado ao callback.
 * @return Um ponteiro para o estado da requisição em caso de sucesso, ou NULL em caso de falha.
 * O chamador é responsável por chamar http_client_abort_request() neste ponteiro
 * se a requisição precisar ser cancelada (ex: por timeout).
 */
HTTP_REQUEST_STATE* http_get_request_cb(const char *host, const char *path, uint16_t port,
                                        http_response_cb_t cb, void *user_ctx);

/**
 * @brief Inicia uma requisição POST assíncrona com corpo binário.
 * * @param host O hostname ou IP do servidor.
 * @param path O caminho do recurso.
 * @param port A porta do servidor.
 * @param content_type O tipo de conteúdo (ex: "audio/adpcm").
 * @param data_ptr Um ponteiro para os dados binários.
 * @param data_len O tamanho dos dados binários.
 * @param cb A função de callback a ser chamada com a resposta.
 * @param user_ctx Um ponteiro de usuário para ser passado ao callback.
 * @return Um ponteiro para o estado da requisição em caso de sucesso, ou NULL em caso de falha.
 * O chamador é responsável por chamar http_client_abort_request() se necessário.
 */
HTTP_REQUEST_STATE* http_post_binary_request_cb(const char *host, const char *path, uint16_t port,
                                                const char *content_type,
                                                const uint8_t *data_ptr, size_t data_len,
                                                http_response_cb_t cb, void *user_ctx);

/**
 * @brief Aborta uma requisição HTTP em andamento e libera seus recursos.
 * * @param state O ponteiro de estado retornado por http_get_request_cb ou http_post_binary_request_cb.
 * Se for NULL, a função não faz nada.
 */
void http_client_abort_request(HTTP_REQUEST_STATE *state);


// --- Funções legadas (não recomendadas para novos usos) ---
err_t http_get_request(const char *host, const char *path, uint16_t port);
err_t http_post_request(const char *host, const char *path, uint16_t port, const char *data);
err_t http_patch_request(const char *host, const char *path, uint16_t port, const char *data);
err_t http_post_binary_request(const char *host, const char *path, uint16_t port,
                               const char *content_type,
                               const uint8_t *data_ptr, size_t data_len);


#endif // HTTP_CLIENT_H