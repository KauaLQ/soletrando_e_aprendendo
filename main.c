#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/adc.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "display/ssd1306_i2c.h"
#include "matriz_led/neopixel_pio.h"
#include "buzzer/buzzer_pwm.h"
#include "network/http_client.h"
#include "network/wifi_manager.h"

// Declare uma variável global (ou no escopo do main) para guardar a requisição ativa
HTTP_REQUEST_STATE *active_request = NULL;
HTTP_REQUEST_STATE *active_post = NULL;

// Defina o host/porta do seu servidor Python
#define SERVER_HOST "192.168.1.104"
#define SERVER_PORT 8000

// global para armazenar palavra vinda do servidor
char server_word[128];
volatile bool server_word_ready = false;
// para coordenar resposta do POST
char current_expected[128];        // palavra que esperamos (copiada antes do POST)
char server_recognized[128];       // texto retornado pelo servidor (to_send/recognized)
volatile bool response_received = false;

// --- Área de renderização do display ---
struct render_area frame_area = {
    start_col: 0,
    end_col : SSD1306_WIDTH - 1,
    start_page : 0,
    end_page : SSD1306_NUM_PAGES - 1
};

// Buffer para o display
uint8_t buf[SSD1306_BUF_LEN];

// --- Definições ---
#define BUTTON_PIN_A 5
#define BUTTON_PIN_B 6
#define BUZZER_PIN_A 21
#define ADC_PIN 28
#define SAMPLE_RATE_HZ 8000
#define MAX_LINE_LEN 128
#define AUDIO_BUFFER_SIZE (SAMPLE_RATE_HZ * 5)  // até 3s de áudio

volatile bool capturando = false;
volatile bool analisando = false;

int nivel = 1;
const int tempo_por_nivel[] = {10, 5, 3};
const int max_nivel = 3;

// --- Buffers de áudio ---
uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
volatile uint32_t audio_index = 0;

uint8_t compressed_data[AUDIO_BUFFER_SIZE];
size_t compressed_size = 0;

// --- Tabelas ADPCM ---
static const int index_table[16] = {
   -1, -1, -1, -1, 2, 4, 6, 8,
   -1, -1, -1, -1, 2, 4, 6, 8
};

static const int step_table[89] = {
     7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,
    34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
   157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
   724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,
  3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
 15289,16818,18500,20350,22385,24623,27086,29794,32767
};

// --- Estrutura do estado ADPCM ---
typedef struct {
    int16_t prev_sample;
    int index;
} adpcm_state_t;

// --- Funções ADPCM ---
static uint8_t adpcm_encode_sample(int16_t sample, adpcm_state_t *state) {
    int diff = sample - state->prev_sample;
    int step = step_table[state->index];
    int code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }

    if (diff >= step) { code |= 4; diff -= step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; }
    if (diff >= step >> 2) { code |= 1; }

    int diffq = step >> 3;
    if (code & 4) diffq += step;
    if (code & 2) diffq += step >> 1;
    if (code & 1) diffq += step >> 2;

    if (code & 8)
        state->prev_sample -= diffq;
    else
        state->prev_sample += diffq;

    if (state->prev_sample > 32767) state->prev_sample = 32767;
    else if (state->prev_sample < -32768) state->prev_sample = -32768;

    state->index += index_table[code];
    if (state->index < 0) state->index = 0;
    if (state->index > 88) state->index = 88;

    return code & 0x0F;
}

void compress_audio_adpcm(uint8_t *input, size_t len, uint8_t *output, size_t *out_len) {
    adpcm_state_t state = {0, 0};
    size_t out_idx = 0;

    for (size_t i = 0; i < len; i += 2) {
        int16_t s1 = ((int16_t)input[i] - 128) << 8;
        uint8_t nib1 = adpcm_encode_sample(s1, &state);
        uint8_t nib2 = 0;

        if (i + 1 < len) {
            int16_t s2 = ((int16_t)input[i + 1] - 128) << 8;
            nib2 = adpcm_encode_sample(s2, &state);
        }

        output[out_idx++] = (nib2 << 4) | nib1;
    }

    *out_len = out_idx;
}

// --- Callback de captura de áudio ---
bool audio_sample_callback(repeating_timer_t *t) {
    if (!capturando) return true;

    if (audio_index < AUDIO_BUFFER_SIZE) {
        uint16_t raw = adc_read();
        uint8_t sample = raw >> 4;  // 12 bits → 8 bits
        audio_buffer[audio_index++] = sample;
    } else {
        capturando = false;  // evita overflow
    }
    return true;
}

// função simples para URL-encode (apenas caracteres comuns; amplia conforme precisar)
static void url_encode(const char *src, char *dst, size_t dst_len) {
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 4 < dst_len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
            ('0' <= c && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else if (c == ' ') {
            // prefira %20; alguns servidores aceitam + mas %20 é mais seguro
            dst[di++] = '%'; dst[di++] = '2'; dst[di++] = '0';
        } else {
            // codifica byte em hex
            const char hex[] = "0123456789ABCDEF";
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0xF];
            dst[di++] = hex[c & 0xF];
        }
    }
    dst[di] = '\0';
}

// parser simples: extrai valor do campo "word" em JSON (não é um parser full JSON)
static void parse_word_from_json(const char *body, size_t len, char *out, size_t out_len) {
    out[0] = '\0';
    const char *s = body;
    const char *end = body + len;
    // procura por "word"
    while (s < end) {
        if (s + 6 < end && s[0] == '"' && s[1] == 'w' && s[2] == 'o' && s[3] == 'r' && s[4] == 'd' && s[5] == '"') {
            const char *p = s + 6;
            // pula espaços até ':'
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == ':') p++;
            // pula espaços até '"'
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == '"') {
                p++;
                // copia até próxima aspas
                size_t oi = 0;
                while (p < end && *p != '"' && oi + 1 < out_len) {
                    out[oi++] = *p++;
                }
                out[oi] = '\0';
                return;
            }
        }
        s++;
    }
    // se não achou, tenta achar sem aspas (numero etc)
    strncpy(out, "", out_len);
}

// callback que será chamado pelo http_client quando o body chegar
static void my_http_get_cb(const char *body, size_t len, void *user_ctx) {
    // body NÃO tem headers aqui, é apenas o corpo
    char tmp[128];
    parse_word_from_json(body, len, tmp, sizeof(tmp));
    if (tmp[0] != '\0') {
        // copia para global e marca pronto
        strncpy(server_word, tmp, sizeof(server_word)-1);
        server_word[sizeof(server_word)-1] = '\0';
        server_word_ready = true;
    } else {
        // fallback: copia parte do body
        size_t cp = len < sizeof(server_word)-1 ? len : sizeof(server_word)-1;
        memcpy(server_word, body, cp);
        server_word[cp] = '\0';
        server_word_ready = true;
    }
}

// callback chamado pelo http_client quando o POST retornar
static void my_post_response_cb(const char *body, size_t len, void *user_ctx) {
    // body é o corpo JSON retornado pelo servidor (por ex: {"recognized":"intua",...})
    // vamos extrair o campo "to_send" (ou "recognized") com parser simples
    printf("entrando em my_post_response_cb");
    const char *s = body;
    const char *end = body + len;
    char tmp[128] = {0};

    // procura "to_send"
    const char *key = "\"to_send\"";
    const char *p = strstr(body, key);
    if (!p) {
        // tenta "recognized"
        key = "\"recognized\"";
        p = strstr(body, key);
    }
    if (p) {
        p += strlen(key);
        // procura ':' depois
        while (p < end && *p != ':') p++;
        if (p < end && *p == ':') p++;
        // pula espaços
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p < end && *p == '"') {
            p++;
            size_t i = 0;
            while (p < end && *p != '"' && i + 1 < sizeof(tmp)) {
                tmp[i++] = *p++;
            }
            tmp[sizeof(tmp)-1] = '\0';
        } else {
            // número ou palavra sem aspas
            size_t i = 0;
            while (p < end && *p != ',' && *p != '}' && i + 1 < sizeof(tmp)) {
                if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') tmp[i++] = *p;
                p++;
            }
            tmp[sizeof(tmp)-1] = '\0';
        }
    } else {
        // fallback: copia parte do body
        size_t cp = len < sizeof(tmp)-1 ? len : sizeof(tmp)-1;
        memcpy(tmp, body, cp);
        tmp[cp] = '\0';
    }

    // salva resultado globalmente e marca pronto
    strncpy(server_recognized, tmp, sizeof(server_recognized)-1);
    server_recognized[sizeof(server_recognized)-1] = '\0';
    response_received = true;

    printf("saindo de my_post_response_cb response_received=%d", response_received);
}

// --- Funções auxiliares do jogo ---
void reset_jogo() {
    nivel = 1;
    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 20, 24, "GAME OVER");
    render(buf, &frame_area);
}

void process_received_line(char* line, char* buffer) {
    if (strstr(line, buffer) != NULL) {
        memset(buf, 0, SSD1306_BUF_LEN);
        WriteString(buf, 5, 8, "Parabens!");
        WriteString(buf, 5, 24, "Certa resposta");
        render(buf, &frame_area);
        npWriteV();
        beep(BUZZER_PIN_A, 120, 400);
        sleep_ms(200);
        beep(BUZZER_PIN_A, 120, 400);

        if (nivel <= max_nivel) {
            nivel++;
            if (nivel == 2) {
                WriteString(buf, 5, 40, "proximo nivel=");
                WriteString(buf, 5, 56, "Nivel 2, 5 segs");
                render(buf, &frame_area);
            } else if (nivel == 3) {
                WriteString(buf, 5, 40, "proximo nivel=");
                WriteString(buf, 5, 56, "Nivel 3, 3 segs");
                render(buf, &frame_area);
            } else {
                nivel = 1;
                WriteString(buf, 5, 40, "Jogo completo!");
                WriteString(buf, 5, 56, "Pressione B");
                render(buf, &frame_area);
            }
        }
        analisando = false;
    } else {
        memset(buf, 0, SSD1306_BUF_LEN);
        WriteString(buf, 5, 8, "resposta foi=");
        WriteString(buf, 5, 24, line);
        WriteString(buf, 5, 40, "palavra era=");
        WriteString(buf, 5, 56, buffer);
        render(buf, &frame_area);
        npWriteX();
        beep(BUZZER_PIN_A, 100, 1000);
        sleep_ms(5000);
        reset_jogo();
        analisando = false;
    }
}

// --- Globals moved out so helper can access them ---
char input_line[MAX_LINE_LEN];
int input_pos = 0;

// --- Helper: executa todo o fluxo que antes estava dentro do "if (ch == '\\n')" ---
// Recebe a palavra no 'buffer' e executa contagem, espera botão A, grava, comprime e envia.
void start_round_from_buffer(char *buffer) {
    // mostra a palavra no display (igual comportamento anterior)
    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 0, 32, buffer);
    render(buf, &frame_area);

    int tempo = tempo_por_nivel[nivel-1] + 1;
    for (uint8_t i = tempo; i > 0; i--) {
        npWriteNumber(i-1);
        beep(BUZZER_PIN_A, (i-1 > 5 ? 130 : (i-1 > 0 ? 110 : 100)), (i-1 > 0 ? 500 : 1000));
        sleep_ms(i-1 > 0 ? 500 : 0);
    }

    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 5, 8, "Pressione A");
    WriteString(buf, 5, 24, "para iniciar");
    WriteString(buf, 5, 40, "a soletrar");
    render(buf, &frame_area);
    npWriteLeft();

    // espera o botão A ser pressionado
    while (gpio_get(BUTTON_PIN_A)) sleep_ms(10);

    sleep_ms(10);
    audio_index = 0;
    capturando = true;

    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 5, 32, "Gravando...");
    render(buf, &frame_area);
    npWriteFace();
    sleep_ms(400);

    // espera soltar botão A
    while (gpio_get(BUTTON_PIN_A)) sleep_ms(10);
    sleep_ms(10);

    capturando = false;
    analisando = true;

    // --- Compressão ADPCM ---
    compress_audio_adpcm(audio_buffer, audio_index, compressed_data, &compressed_size);
    printf("Compressão concluída: %lu bytes\n", compressed_size);

    // copia expected para global para ser usada no processamento final
    strncpy(current_expected, buffer, sizeof(current_expected)-1);
    current_expected[sizeof(current_expected)-1] = '\0';
    response_received = false;
    server_recognized[0] = '\0';

    // Se já existir uma requisição ativa de uma tentativa anterior, aborte-a primeiro!
    if (active_post) {
        http_client_abort_request(active_post);
        active_post = NULL;
    }

    // monta path e faz POST binário com callback
    char expected_enc[128];
    url_encode(buffer, expected_enc, sizeof(expected_enc)); // 'buffer' era a palavra pedida
    char path_post[256];
    snprintf(path_post, sizeof(path_post), "/upload_audio_raw?nivel=%d&expected=%s", nivel, expected_enc);

    if (http_post_binary_request_cb(SERVER_HOST, path_post, SERVER_PORT, "audio/adpcm",
                                    compressed_data, compressed_size,
                                    my_post_response_cb, NULL) == ERR_OK) {
        printf("POST audio/adpcm enviado (async): %lu bytes\n", (unsigned long)compressed_size);
    } else {
        printf("Erro ao enviar POST audio\n");
    }

    // Chame a função e guarde o ponteiro de estado
    active_post = http_post_binary_request_cb(SERVER_HOST, path_post, SERVER_PORT, "audio/adpcm",
                                    compressed_data, compressed_size,
                                    my_post_response_cb, NULL);
    if (active_post) {
        printf("POST audio/adpcm enviado (async): %lu bytes\n", (unsigned long)compressed_size);
    } else {
        printf("Erro ao enviar POST audio\n"); 
    }

    // mostra "Processando..."
    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 5, 32, "Processando...");
    render(buf, &frame_area);

    // aguarda resposta do servidor (timeout ~25s)
    absolute_time_t t0 = get_absolute_time();
    while (!response_received && absolute_time_diff_us(t0, get_absolute_time()) < 25 * 1000000) {
        cyw43_arch_poll();
        tight_loop_contents();  // micro pause (ajuda o RTOS cooperativo)
        sleep_us(1000);         // ~1ms entre polls
    }

    if (response_received) {
        active_post = NULL; // A requisição terminou com sucesso, limpe o ponteiro
        printf("Resposta do servidor recebida: %s\n", server_recognized);
        // chama o mesmo processamento que antes (process_received_line)
        process_received_line(server_recognized, current_expected);
        for (int i = 0; i < 200; i++) {
            cyw43_arch_poll();
            sleep_ms(1);
        }
    } else {
        printf("Timeout aguardando resposta do servidor (POST)\n");
        // Se deu timeout, aborte a requisição para liberar os recursos
        http_client_abort_request(active_post);
        active_post = NULL; // Limpe o ponteiro
        analisando = false;
    }
}

// --- Função principal ---
int main() {
    stdio_init_all();
    sleep_ms(3000);

    // Verifique se a reinicialização foi causada pelo watchdog (útil para lógica pós-reset)
    if (watchdog_caused_reboot()) {
        printf("Reinicializado pelo Watchdog!\n");
    } else {
        printf("Inicialização Limpa (Power-on ou Reset Manual)\n");
    }

    // 1. Inicializa o hardware do Wi-Fi
    printf("Inicializando hardware Wi-Fi...\n");
    if (!wifi_arch_init()) {
        printf("ERRO: Falha ao inicializar hardware Wi-Fi!\n");
        return -1;
    }

	// 2. Tenta conectar à rede
    wifi_connect();

    // Inicialização do ADC
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(2); // GPIO28 = ADC2

    // Inicializa LED e display
    npInit(7);
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    SSD1306_init();

    memset(buf, 0, SSD1306_BUF_LEN);
    calc_render_area_buflen(&frame_area);
    render(buf, &frame_area);

    // Botões
    gpio_init(BUTTON_PIN_A);
    gpio_set_dir(BUTTON_PIN_A, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_A);
    gpio_init(BUTTON_PIN_B);
    gpio_set_dir(BUTTON_PIN_B, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_B);

    // Timer de captura
    repeating_timer_t timer;
    add_repeating_timer_us(-1000000 / SAMPLE_RATE_HZ, audio_sample_callback, NULL, &timer);

    char buffer[100];
    int idx = 0;
    bool esperando = true;

    // Tela inicial
    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 5, 8, "Pressione B");
    WriteString(buf, 5, 24, "para iniciar");
    WriteString(buf, 5, 40, "o jogo");
    render(buf, &frame_area);
    npWriteRigth();

    while (true) {
        if (esperando && !gpio_get(BUTTON_PIN_B)) {
            // Se já existir uma requisição ativa de uma tentativa anterior, aborte-a primeiro!
            if (active_request) {
                http_client_abort_request(active_request);
                active_request = NULL;
            }
            // 1. LIMPE A FLAG ANTES DE FAZER A REQUISIÇÃO
            server_word_ready = false;

            // solicita a palavra ao servidor usando callback
            char path_get[128];
            printf("Solicitando palavra para nivel=%d\n", nivel);
            snprintf(path_get, sizeof(path_get), "/request_word?nivel=%d", nivel);
            // Chame a função e guarde o ponteiro de estado
            active_request = http_get_request_cb(SERVER_HOST, path_get, SERVER_PORT, my_http_get_cb, NULL);
            if (active_request) {
                printf("GET /request_word pedido para nivel=%d enviado.\n", nivel);
            } else {
                printf("Erro ao enviar GET /request_word\n");
                // Pula para a próxima iteração do laço
                continue; 
            }

            // aguarda resposta do servidor (timeout ~20s)
            absolute_time_t t0 = get_absolute_time();
            while (!server_word_ready && absolute_time_diff_us(t0, get_absolute_time()) < 20 * 1000000) {
                cyw43_arch_poll();
                tight_loop_contents();  // micro pause (ajuda o RTOS cooperativo)
                sleep_us(1000);         // ~1ms entre polls
            }

            if (server_word_ready) {
                active_request = NULL; // A requisição terminou com sucesso, limpe o ponteiro
                // copia para buffer local usado pelo fluxo do jogo
                strncpy(buffer, server_word, sizeof(buffer)-1);
                buffer[sizeof(buffer)-1] = '\0';
                server_word_ready = false; // limpa flag
                printf("Palavra recebida: %s\n", buffer);

                // chama o fluxo principal de rodada diretamente
                start_round_from_buffer(buffer);
            } else {
                printf("Timeout esperando palavra do servidor.\n");
                // Se deu timeout, aborte a requisição para liberar os recursos
                http_client_abort_request(active_request);
                active_request = NULL; // Limpe o ponteiro
            }
            esperando = false;
        }

        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == '\n' || ch == '\r') {
                buffer[idx] = '\0';
                idx = 0;

                // Se chegou via serial (caso fallback), usamos o mesmo fluxo:
                start_round_from_buffer(buffer);

            } else if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = (char)ch;
            }
        }

        if (gpio_get(BUTTON_PIN_B)) esperando = true;
        sleep_ms(10);
    }
}