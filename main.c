/* main.c - adaptado para operar via HTTP (cliente)
 *
 * Fluxo:
 *  - B pressionado -> GET /pedir_palavra?nivel=<N>
 *  - Recebe palavra -> mostra na tela, faz contagem, grava áudio em buffer
 *  - Ao terminar gravação -> POST /notify_audio?nivel=<N> (aviso opcional)
 *  - Polling GET /resultado?nivel=<N> até receber transcrição
 *  - Ao receber transcrição -> process_received_line(transcription, buffer)
 *
 * Requisitos: wifi_manager.h e http_client.h (a implementação http que você enviou)
 */

#include <string.h>
#include <stdio.h>      // só para snprintf; não usamos stdio como interface serial
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/binary_info.h"
#include "pico/time.h"
#include "hardware/i2c.h"

#include "display/ssd1306_i2c.h"
#include "matriz_led/neopixel_pio.h"
#include "buzzer/buzzer_pwm.h"

#include "network/wifi_manager.h"
#include "network/http_client.h"

/* --- configurações --- */
#define BUTTON_PIN_A 5
#define BUTTON_PIN_B 6
#define BUZZER_PIN_A 21
#define ADC_PIN 28
#define SAMPLE_RATE_HZ 8000
#define MAX_LINE_LEN 128

#define SERVER_HOST "192.168.1.104"
#define SERVER_PORT 8000

/* --- display area (mantive sua estrutura) --- */
struct render_area frame_area = {
    start_col: 0,
    end_col : SSD1306_WIDTH - 1,
    start_page : 0,
    end_page : SSD1306_NUM_PAGES - 1
};

uint8_t buf[SSD1306_BUF_LEN];

static volatile bool polling_active = false;
static volatile bool polling_request_inflight = false;
static uint32_t next_poll_time_ms = 0;

/* --- jogo / estados --- */
volatile bool capturando = false;
volatile bool analisando = false;

/* novas flags de sessão para evitar reentrada automática */
static volatile bool session_active = false; // true entre /pedir_palavra e processamento final
static volatile bool word_shown = false;     // true depois que a palavra já foi exibida ao usuário

int nivel = 1;
const int tempo_por_nivel[] = {10, 5, 3};
const int max_nivel = 3;

/* --- buffer de áudio (armazenamento local) --- */
#define AUDIO_BUF_LEN (64 * 1024) // 64KiB -> suficiente para ~8kHz * 7s = 56KiB
static volatile uint8_t audio_buffer[AUDIO_BUF_LEN];
static volatile size_t audio_pos = 0;

/* --- variáveis para fluxo HTTP --- */
static char current_expected_word[100] = {0};
static volatile bool waiting_for_word = false;
static volatile bool waiting_for_result = false;

/* Forward: função que será chamada quando http_client receber corpo de resposta.
   Precisamos que http_client.c invoque essa função (veja snippet abaixo). */
void http_client_response_handler(const char *body);

/* --- protótipo do novo POST binário (adicionado em http_client.h) --- */
err_t http_post_binary(const char *host, const char *path, uint16_t port, const uint8_t *data, size_t data_len, const char *content_type);

/* --- monta cabeçalho WAV 8-bit PCM mono 8000Hz (44 bytes) --- */
static void write_wav_header(uint8_t *hdr, uint32_t data_len) {
    // header little-endian
    uint32_t chunk_size = 36 + data_len;
    uint32_t subchunk2_size = data_len;
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 1;
    uint32_t sample_rate = SAMPLE_RATE_HZ;
    uint16_t bits_per_sample = 8;
    uint16_t block_align = (num_channels * bits_per_sample) / 8;
    uint32_t byte_rate = sample_rate * block_align;

    // RIFF
    memcpy(hdr + 0, "RIFF", 4);
    hdr[4] = (uint8_t)(chunk_size & 0xff);
    hdr[5] = (uint8_t)((chunk_size >> 8) & 0xff);
    hdr[6] = (uint8_t)((chunk_size >> 16) & 0xff);
    hdr[7] = (uint8_t)((chunk_size >> 24) & 0xff);
    memcpy(hdr + 8, "WAVE", 4);

    // fmt subchunk
    memcpy(hdr + 12, "fmt ", 4);
    uint32_t subchunk1_size = 16;
    hdr[16] = (uint8_t)(subchunk1_size & 0xff);
    hdr[17] = (uint8_t)((subchunk1_size >> 8) & 0xff);
    hdr[18] = (uint8_t)((subchunk1_size >> 16) & 0xff);
    hdr[19] = (uint8_t)((subchunk1_size >> 24) & 0xff);
    hdr[20] = (uint8_t)(audio_format & 0xff);
    hdr[21] = (uint8_t)((audio_format >> 8) & 0xff);
    hdr[22] = (uint8_t)(num_channels & 0xff);
    hdr[23] = (uint8_t)((num_channels >> 8) & 0xff);
    hdr[24] = (uint8_t)(sample_rate & 0xff);
    hdr[25] = (uint8_t)((sample_rate >> 8) & 0xff);
    hdr[26] = (uint8_t)((sample_rate >> 16) & 0xff);
    hdr[27] = (uint8_t)((sample_rate >> 24) & 0xff);
    hdr[28] = (uint8_t)(byte_rate & 0xff);
    hdr[29] = (uint8_t)((byte_rate >> 8) & 0xff);
    hdr[30] = (uint8_t)((byte_rate >> 16) & 0xff);
    hdr[31] = (uint8_t)((byte_rate >> 24) & 0xff);
    hdr[32] = (uint8_t)(block_align & 0xff);
    hdr[33] = (uint8_t)((block_align >> 8) & 0xff);
    hdr[34] = (uint8_t)(bits_per_sample & 0xff);
    hdr[35] = (uint8_t)((bits_per_sample >> 8) & 0xff);

    // data subchunk
    memcpy(hdr + 36, "data", 4);
    hdr[40] = (uint8_t)(subchunk2_size & 0xff);
    hdr[41] = (uint8_t)((subchunk2_size >> 8) & 0xff);
    hdr[42] = (uint8_t)((subchunk2_size >> 16) & 0xff);
    hdr[43] = (uint8_t)((subchunk2_size >> 24) & 0xff);
}

/* --- monta WAV em heap e envia via HTTP POST binário --- */
// retorna ERR_OK em sucesso (ownership transferida), outro err_t em erro
err_t build_wav_and_send(int nivel_request) {
    // calcula tamanhos
    size_t payload_len = audio_pos; // bytes de 8-bit PCM no buffer
    if (payload_len == 0) return ERR_VAL;

    size_t total_len = 44 + payload_len;
    uint8_t *packet = malloc(total_len);
    if (!packet) {
        // fallback UI
        memset(buf,0,SSD1306_BUF_LEN);
        WriteString(buf, 5, 24, "ERRO MEM WAV");
        render(buf, &frame_area);
        return ERR_MEM;
    }

    // escreve header + payload
    write_wav_header(packet, (uint32_t)payload_len);
    // audio_buffer já contém 8-bit samples (0..255) - copiamos diretamente
    memcpy(packet + 44, (const void*)audio_buffer, payload_len);

    // monta path com nível
    char path[128];
    snprintf(path, sizeof(path), "/upload_audio_raw?nivel=%d", nivel_request);

    // envia (http_post_binary_take_ownership fará a cópia/posse)
    err_t r = http_post_binary_take_ownership(SERVER_HOST, path, SERVER_PORT, packet, total_len, "audio/wav");
    if (r != ERR_OK) {
        // falhou: libera packet (http_client não recebeu ownership)
        free(packet);
        memset(buf,0,SSD1306_BUF_LEN);
        WriteString(buf, 5, 24, "ERRO HTTP SEND");
        render(buf, &frame_area);
    }
    return r;
}

/* --- ADC callback: agora armazena em buffer em vez de enviar pela serial --- */
bool audio_sample_callback(repeating_timer_t *t) {
    if (!capturando) return true;

    uint16_t raw = adc_read();
    uint8_t sample = raw >> 4; // 12 bits -> 8 bits

    if (audio_pos < AUDIO_BUF_LEN) {
        audio_buffer[audio_pos++] = sample;
    }
    return true;
}

/* --- utilitários de UI --- */
void reset_jogo() {
    nivel = 1;
    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 20, 24, "GAME OVER");
    render(buf, &frame_area);
}

/* process_received_line mantém a semântica original; ao final encerra a sessão */
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
        WriteString(buf, 5, 8, "a resposta foi=");
        WriteString(buf, 5, 24, line);
        WriteString(buf, 5, 40, "a palavra era=");
        WriteString(buf, 5, 56, buffer);
        render(buf, &frame_area);
        npWriteX();
        beep(BUZZER_PIN_A, 100, 1000);
        sleep_ms(5000);
        reset_jogo();
        analisando = false;
    }

    // encerra sessão: usuário precisa pedir nova palavra com B
    session_active = false;
    // opcional: limpa current_expected_word aqui (mantive para segurança)
    memset(current_expected_word, 0, sizeof(current_expected_word));
    word_shown = false;
}

/* --- Implementação do handler HTTP que será chamado pelo http_client quando chegar resposta ---
   - Se estivermos esperando a palavra (waiting_for_word), o body será a palavra esperada - guardamos em current_expected_word.
   - Se estivermos esperando resultado (waiting_for_result), tratamos body como transcrição e chamamos process_received_line.
*/
void http_client_response_handler(const char *body) {
    if (!body) return;

    if (waiting_for_word) {
        // copia a palavra recebida e mantemos session_active = true
        strncpy(current_expected_word, body, sizeof(current_expected_word)-1);
        current_expected_word[sizeof(current_expected_word)-1] = '\0';
        waiting_for_word = false;
        // sinaliza que a sessão está ativa & que ainda não mostramos a palavra
        session_active = true;
        word_shown = false;
    } else if (waiting_for_result) {
        // Se servidor respondeu "processing", ainda não é a transcrição final
        if (strstr(body, "processing") != NULL) {
            polling_request_inflight = false;
            uint32_t now = to_ms_since_boot(get_absolute_time());
            next_poll_time_ms = now + 1000;
            return;
        }

        // caso contrário: recebemos a transcrição final
        char transcription[MAX_LINE_LEN];
        strncpy(transcription, body, sizeof(transcription)-1);
        transcription[sizeof(transcription)-1] = '\0';

        // processa com a palavra esperada
        process_received_line(transcription, (char*)current_expected_word);

        // encerra o polling
        waiting_for_result = false;
        polling_active = false;
        polling_request_inflight = false;
    }
}

/* --- Monta e dispara GET para pedir palavra --- */
void pedir_palavra_http(int nivel_request) {
    char path[128];
    snprintf(path, sizeof(path), "/pedir_palavra?nivel=%d", nivel_request);
    // estado: solicitando palavra
    waiting_for_word = true;
    session_active = true;  // sessão aberta, esperando que a palavra venha
    word_shown = false;     // ainda não mostramos a palavra
    http_get_request(SERVER_HOST, path, SERVER_PORT);
}

/* --- Aviso ao servidor que áudio está pronto (opcional) --- */
void notify_audio_ready_http(int nivel_request) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"event\":\"audio_ready\",\"nivel\":%d}", nivel_request);
    http_post_request(SERVER_HOST, "/notify_audio", SERVER_PORT, payload);
}

/* --- Polling para resultado --- */
void start_polling_result_http(int nivel_request) {
    waiting_for_result = true;
    polling_active = true;
    // imediatamente faremos um primeiro GET
    polling_request_inflight = true;
    next_poll_time_ms = to_ms_since_boot(get_absolute_time());
    char path[128];
    snprintf(path, sizeof(path), "/resultado?nivel=%d", nivel_request);
    http_get_request(SERVER_HOST, path, SERVER_PORT);
}

/* --- Função principal --- */
int main() {
    // inicializa stdio para logs serial (opcional). Se quiser remover, troque para sem stdio.
    stdio_init_all();
    sleep_ms(2000);

    // Inicializa ADC (microfone)
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(2); // GPIO28 = ADC2

    // matriz led
    npInit(7);

    // display
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    SSD1306_init();

    memset(buf, 0, SSD1306_BUF_LEN);
    calc_render_area_buflen(&frame_area);
    render(buf, &frame_area);

    // botões
    gpio_init(BUTTON_PIN_A);
    gpio_set_dir(BUTTON_PIN_A, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_A);
    gpio_init(BUTTON_PIN_B);
    gpio_set_dir(BUTTON_PIN_B, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_B);

    // iniciar amostragem periódica
    repeating_timer_t timer;
    add_repeating_timer_us(-1000000 / SAMPLE_RATE_HZ, audio_sample_callback, NULL, &timer);

    // inicializa Wi-Fi
    if (!wifi_arch_init()) {
        memset(buf,0,SSD1306_BUF_LEN);
        WriteString(buf, 5, 24, "ERRO WIFI INIT");
        render(buf, &frame_area);
        while(true) sleep_ms(1000);
    }
    if (!wifi_connect()) {
        memset(buf,0,SSD1306_BUF_LEN);
        WriteString(buf, 5, 24, "ERRO WIFI CONN");
        render(buf, &frame_area);
        while(true) sleep_ms(1000);
    }

    // estado de espera inicial
    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 5, 8, "pressione B");
    WriteString(buf, 5, 24, "para iniciar");
    WriteString(buf, 5, 40, "o jogo");
    render(buf, &frame_area);
    npWriteRigth();

    bool esperando = true;

    while (true) {
        // botão B pedido de palavra
        if (esperando && !gpio_get(BUTTON_PIN_B)) {
            // Ao pedir nova palavra, limpamos estado anterior explicitamente
            memset(current_expected_word, 0, sizeof(current_expected_word));
            session_active = true;
            word_shown = false;

            // pede palavra ao servidor via GET
            pedir_palavra_http(nivel);
            esperando = false;

            // mostra tela de aguardando
            memset(buf,0,SSD1306_BUF_LEN);
            WriteString(buf, 5, 8, "pedindo palavra...");
            render(buf, &frame_area);
        }

        // quando a palavra chega (session_active==true) e ainda não mostramos ela,
        // e não estamos no modo 'analisando'
        if (session_active && !waiting_for_word && strlen(current_expected_word) > 0 && !analisando && !word_shown) {
            // mostramos a palavra recebida (como no fluxo original)
            memset(buf, 0, SSD1306_BUF_LEN);
            WriteString(buf, 0, 32, current_expected_word);
            render(buf, &frame_area);

            // marcamos que já mostramos (evita reentrada automática)
            word_shown = true;

            int tempo = tempo_por_nivel[nivel-1] + 1;

            // contador na matriz de leds
            for (uint8_t i = tempo; i > 0; i--) {
                npWriteNumber(i-1);
                beep(BUZZER_PIN_A, (i-1 > 5 ? 130 : (i-1 > 0 ? 110 : 100)), (i-1 > 0 ? 500 : 1000));
                sleep_ms(i-1 > 0 ? 500 : 0);
            }

            memset(buf, 0, SSD1306_BUF_LEN);
            WriteString(buf, 5, 8, "pressione A");
            WriteString(buf, 5, 24, "para iniciar");
            WriteString(buf, 5, 40, "a soletrar");
            render(buf, &frame_area);
            npWriteLeft();

            // espera o toque em A (pressione A para iniciar)
            while (gpio_get(BUTTON_PIN_A)) sleep_ms(10); // espera até pressionar (active low)
            sleep_ms(50); // debounce inicial

            // começa gravação - usuário pressionou A
            audio_pos = 0;
            capturando = true;
            memset(buf, 0, SSD1306_BUF_LEN);
            WriteString(buf, 5, 32, "gravando...");
            render(buf, &frame_area);
            npWriteFace();

            // aguarda liberação do botão (evitar capturar o mesmo evento de press)
            while (!gpio_get(BUTTON_PIN_A)) sleep_ms(10);
            sleep_ms(50); // debounce

            // agora espera o próximo PRESS de A para encerrar (press-to-stop)
            while (gpio_get(BUTTON_PIN_A)) sleep_ms(10); // espera até pressionar de novo
            sleep_ms(50); // debounce antes de parar

            // para gravação (press again -> stop)
            capturando = false;
            analisando = true;

            memset(buf, 0, SSD1306_BUF_LEN);
            WriteString(buf, 5, 24, "audio gravado");
            WriteString(buf, 5, 40, "processando...");
            render(buf, &frame_area);

            // notifica servidor (opcional)
            // notify_audio_ready_http(nivel);

            err_t send_r = build_wav_and_send(nivel);
            if (send_r == ERR_OK) {
                // inicia polling apenas se o POST foi aceito
                start_polling_result_http(nivel);
            } else {
                // falha no envio: informa usuário e cancela análise
                analisando = false;
            }

            // agora esperamos que waiting_for_result e http_client_response_handler
            // chamem process_received_line quando receberem a transcrição.
            // enquanto isso, mantemos o loop rodando para que botões ainda funcionem.
        }

        // Reset do botão B (se solto, re-armar o 'esperando' para novo pedido)
        if (gpio_get(BUTTON_PIN_B)) {
            esperando = true;
        }
        // A palavra será limpa quando o usuário pedir nova palavra (apertar B)
        // ou ao finalizar process_received_line().

        // checar polling: se ativo, não há request em flight e já passou o tempo -> enviar novo GET
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (polling_active && !polling_request_inflight && (int32_t)(now - next_poll_time_ms) >= 0) {
            // envia novo GET
            char path[128];
            snprintf(path, sizeof(path), "/resultado?nivel=%d", nivel);
            polling_request_inflight = true;
            http_get_request(SERVER_HOST, path, SERVER_PORT);
            // por segurança atualizamos next_poll_time para evitar envio imediato repetido;
            // será reajustado no handler caso receba "processing".
            next_poll_time_ms = now + 5000; // fallback (5s) caso handler não ajuste — proteção
        }

        sleep_ms(50);
    }

    return 0;
}