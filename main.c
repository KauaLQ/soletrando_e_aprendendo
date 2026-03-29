#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"

#include "drivers/display_2.0/ssd1306_i2c.h"
#include "drivers/matriz_led/neopixel_pio.h"
#include "drivers/buzzer/buzzer_pwm.h"

#define LED_MATRIX 7
#define BUTTON_PIN_A 5
#define BUTTON_PIN_B 6
#define BUZZER_PIN_A 21
#define ADC_PIN 28
#define SAMPLE_RATE_HZ 8000
#define MAX_LINE_LEN 128
#define AUDIO_BUFFER_SIZE 512

// Variáveis de áudio
uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
int dma_chan;

// Variáveis globais de nível
int nivel = 1;
const int tempo_por_nivel[] = {10, 5, 3}; // segs de contagem para cada nível
const int max_nivel = 3; // limite máximo de níveis
volatile bool analisando = false;

// Protótipo das funções
void setup_adc_dma();
void iniciar_captura();
void parar_captura();
void reset_jogo();                                      // Função para resetar jogo
void process_received_line(char* line, char* buffer);   // Processa a resposta da uart

int main()
{
    stdio_init_all();
    sleep_ms(5000);
    // Configura ADC + DMA
    setup_adc_dma();

    // matriz de led
    npInit(LED_MATRIX);

    //configuração do display ssd1306
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    SSD1306_init();

    // Botão
    gpio_init(BUTTON_PIN_A);
    gpio_set_dir(BUTTON_PIN_A, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_A);
    gpio_init(BUTTON_PIN_B);
    gpio_set_dir(BUTTON_PIN_B, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_B);

    char buffer[100];
    int idx = 0;
    char input_line[MAX_LINE_LEN];
    int input_pos = 0;
    bool esperando = true;

    SSD1306_clear();
    SSD1306_draw_string(5, 8, "pressione B");
    SSD1306_draw_string(5, 24, "para iniciar");
    SSD1306_draw_string(5, 40, "o jogo");
    SSD1306_update();
    npWriteRigth();

    while (true) {
        if (esperando && !gpio_get(BUTTON_PIN_B)) {
            printf("pedir_palavra %d\n", nivel);
            esperando = false;  // evita múltiplos envios com botão pressionado
        }

        // Lê resposta do PC
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == '\n' || ch == '\r') {
                buffer[idx] = '\0';
                SSD1306_clear();
                SSD1306_draw_string(0, 32, buffer);
                SSD1306_update();
                idx = 0;

                int tempo = tempo_por_nivel[nivel-1] + 1;

                //desenhando na matriz de led
                for (uint8_t i = tempo; i > 0; i--) {
                    npWriteNumber(i-1);
                    beep(BUZZER_PIN_A, (i-1 > 5 ? 130 : (i-1 > 0 ? 110 : 100)), (i-1 > 0 ? 500 : 1000));
                    sleep_ms(i-1 > 0 ? 500 : 0);
                }

                SSD1306_clear();
                SSD1306_draw_string(5, 8, "pressione A");
                SSD1306_draw_string(5, 24, "para iniciar");
                SSD1306_draw_string(5, 40, "a soletrar");
                SSD1306_update();
                npWriteLeft();

                while (gpio_get(BUTTON_PIN_A)) { sleep_ms(10); }
                sleep_ms(10);
                
                iniciar_captura();
                SSD1306_clear();
                SSD1306_draw_string(5, 32, "gravando...");
                SSD1306_update();
                npWriteFace();

                // Aplica o debounce após a ação inicial do botão
                sleep_ms(400);
                while (gpio_get(BUTTON_PIN_A))
                {
                    if (!dma_channel_is_busy(dma_chan)) {
                        for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
                            putchar_raw(audio_buffer[i]);
                        }
                        dma_channel_set_write_addr(dma_chan, audio_buffer, true);
                    }
                    sleep_ms(1);
                }
                sleep_ms(10);

                parar_captura();
                analisando = !analisando;
                SSD1306_clear();
                SSD1306_draw_string(5, 24, "audio gravado");
                SSD1306_draw_string(5, 40, "processando...");
                SSD1306_update();

                // Aplica o debounce após a ação inicial do botão
                sleep_ms(400);

                while (analisando)
                {
                    int c = getchar_timeout_us(0);  // 0 = sem esperar
                    if (c != PICO_ERROR_TIMEOUT) {
                        if (c == '\n' || c == '\r') {
                            input_line[input_pos] = '\0';
                            process_received_line(input_line, buffer);
                            input_pos = 0;
                        } else if (input_pos < MAX_LINE_LEN - 1) {
                            input_line[input_pos++] = (char)c;
                        }
                    }
                    sleep_ms(10);
                }

            } else if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = (char)ch;
            }
        }

        // Espera botão soltar
        if (gpio_get(BUTTON_PIN_B)) {
            esperando = true;
        }
        sleep_ms(10);
    }
}

void setup_adc_dma(){
    // ADC - Microfone
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(2);

    // ADC FIFO
    adc_fifo_setup(true, true, 1, false, true);

    // taxa de amostragem (~8kHz)
    adc_set_clkdiv(48000000 / SAMPLE_RATE_HZ);

    dma_chan = dma_claim_unused_channel(true);

    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(
        dma_chan,
        &cfg,
        audio_buffer,      // destino
        &adc_hw->fifo,     // origem
        AUDIO_BUFFER_SIZE, // número de samples
        false              // não começa ainda
    );
}

void iniciar_captura() {
    adc_run(true);
    dma_channel_set_write_addr(dma_chan, audio_buffer, true);
}

void parar_captura() {
    adc_run(false);
    dma_channel_abort(dma_chan);
}

void reset_jogo() {
    nivel = 1;
    SSD1306_clear();
    SSD1306_draw_string(20, 24, "GAME OVER");
    SSD1306_update();
}

void process_received_line(char* line, char* buffer) {
    if (strstr(line, buffer) != NULL) {
        SSD1306_clear();
        SSD1306_draw_string(5, 8, "Parabens!");
        SSD1306_draw_string(5, 24, "Certa resposta");
        SSD1306_update();
        npWriteV();
        beep(BUZZER_PIN_A, 120, 400);
        sleep_ms(200);
        beep(BUZZER_PIN_A, 120, 400);

        if (nivel <= max_nivel) {
            nivel++;
            if (nivel == 2) {
                SSD1306_draw_string(5, 40, "prroximo nivel=");
                SSD1306_draw_string(5, 56, "Nivel 2, 5 segs");
                SSD1306_update();
            } else if (nivel == 3) {
                SSD1306_draw_string(5, 40, "prroximo nivel=");
                SSD1306_draw_string(5, 56, "Nivel 3, 3 segs");
                SSD1306_update();
            } else {
                nivel = 1;
                SSD1306_draw_string(5, 40, "Jogo completo!");
                SSD1306_draw_string(5, 56, "Pressione B");
                SSD1306_update();
            }
        }
        analisando = false; // sai do loop e vai pro próximo
    } else {
        SSD1306_clear();
        SSD1306_draw_string(5, 8, "a resposta foi=");
        SSD1306_draw_string(5, 24, line);
        SSD1306_draw_string(5, 40, "a palavra era=");
        SSD1306_draw_string(5, 56, buffer);
        SSD1306_update();
        npWriteX();
        beep(BUZZER_PIN_A, 100, 1000);
        sleep_ms(5000);
        reset_jogo();
        analisando = false; // volta para pedir palavra de novo
    }
}