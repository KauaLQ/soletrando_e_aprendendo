#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/adc.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "display/ssd1306_i2c.h"
#include "matriz_led/neopixel_pio.h"
#include "buzzer/buzzer_pwm.h"

// Área de renderização do display
struct render_area frame_area = {
    start_col: 0,
    end_col : SSD1306_WIDTH - 1,
    start_page : 0,
    end_page : SSD1306_NUM_PAGES - 1
    };

// Buffer para o display
uint8_t buf[SSD1306_BUF_LEN];

#define BUTTON_PIN_A 5
#define BUTTON_PIN_B 6
#define BUZZER_PIN_A 21

#define ADC_PIN 28
#define SAMPLE_RATE_HZ 8000
#define MAX_LINE_LEN 128

volatile bool capturando = false;
volatile bool analisando = false;

bool audio_sample_callback(repeating_timer_t *t) {
    if (!capturando) return true;

    uint16_t raw = adc_read();
    uint8_t sample = raw >> 4; // 12 bits → 8 bits
    putchar_raw(sample);
    return true;
}

void process_received_line(char* line, char* buffer) {
    // Aqui você pode comparar "line" e executar comandos:
    if (strstr(line, buffer) != NULL) {
        memset(buf, 0, SSD1306_BUF_LEN);
        WriteString(buf, 5, 24, "parabens");
        WriteString(buf, 5, 40, "certa resposta");
        render(buf, &frame_area);
        analisando = !analisando;
    }
    else {
        memset(buf, 0, SSD1306_BUF_LEN);
        WriteString(buf, 5, 8, "a resposta foi:");
        WriteString(buf, 5, 24, line);
        WriteString(buf, 5, 40, "a palavra era:");
        WriteString(buf, 5, 56, buffer);
        render(buf, &frame_area);
        npWriteX();
        beep(BUZZER_PIN_A, 100, 1000);
        analisando = !analisando;
    }
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    // ADC - Microfone
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(2); // GPIO28 = ADC2

    // matriz de led
    npInit(7); // ou LED_PIN, se definir no header

    //configuração do display ssd1306
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    SSD1306_init();

    memset(buf, 0, SSD1306_BUF_LEN);
    calc_render_area_buflen(&frame_area);

    render(buf, &frame_area);

    // Botão
    gpio_init(BUTTON_PIN_A);
    gpio_set_dir(BUTTON_PIN_A, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_A);
    gpio_init(BUTTON_PIN_B);
    gpio_set_dir(BUTTON_PIN_B, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_B);

    // Inicia amostragem periódica
    repeating_timer_t timer;
    add_repeating_timer_us(-1000000 / SAMPLE_RATE_HZ, audio_sample_callback, NULL, &timer);

    char buffer[100];
    int idx = 0;
    bool esperando = true;

    char input_line[MAX_LINE_LEN];
    int input_pos = 0;

    memset(buf, 0, SSD1306_BUF_LEN);
    WriteString(buf, 5, 8, "pressione B");
    WriteString(buf, 5, 24, "para iniciar");
    WriteString(buf, 5, 40, "o jogo");
    render(buf, &frame_area);
    npWriteRigth();

    while (true) {
        if (esperando && !gpio_get(BUTTON_PIN_B)) {
            printf("pedir_palavra\n");
            esperando = false;  // evita múltiplos envios com botão pressionado
        }

        // Lê resposta do PC
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == '\n' || ch == '\r') {
                buffer[idx] = '\0';
                memset(buf, 0, SSD1306_BUF_LEN);
                WriteString(buf, 0, 32, buffer);
                render(buf, &frame_area);
                idx = 0;
                npWriteTen();
                beep(BUZZER_PIN_A, 130, 500);
                sleep_ms(500);
                npWriteNine();
                beep(BUZZER_PIN_A, 130, 500);
                sleep_ms(500);
                npWriteEigth();
                beep(BUZZER_PIN_A, 130, 500);
                sleep_ms(500);
                npWriteSeven();
                beep(BUZZER_PIN_A, 130, 500);
                sleep_ms(500);
                npWriteSix();
                beep(BUZZER_PIN_A, 130, 500);
                sleep_ms(500);
                npWriteFive();
                beep(BUZZER_PIN_A, 110, 500);
                sleep_ms(500);
                npWriteFour();
                beep(BUZZER_PIN_A, 110, 500);
                sleep_ms(500);
                npWriteThree();
                beep(BUZZER_PIN_A, 110, 500);
                sleep_ms(500);
                npWriteTwo();
                beep(BUZZER_PIN_A, 110, 500);
                sleep_ms(500);
                npWriteOne();
                beep(BUZZER_PIN_A, 110, 500);
                sleep_ms(500);
                npWriteZero();
                beep(BUZZER_PIN_A, 100, 1000);

                memset(buf, 0, SSD1306_BUF_LEN);
                WriteString(buf, 5, 8, "pressione A");
                WriteString(buf, 5, 24, "para iniciar");
                WriteString(buf, 5, 40, "a soletrar");
                render(buf, &frame_area);

                npWriteLeft();

                while (gpio_get(BUTTON_PIN_A))
                {
                    sleep_ms(10);
                }

                sleep_ms(10);
                
                capturando = !capturando;  // inverte estado
                memset(buf, 0, SSD1306_BUF_LEN);
                WriteString(buf, 5, 32, "gravando...");
                render(buf, &frame_area);
                npWriteFace();

                // Aplica o debounce após a ação inicial do botão
                sleep_ms(400);

                while (gpio_get(BUTTON_PIN_A))
                {
                    sleep_ms(10);
                }

                sleep_ms(10);

                capturando = !capturando;  // inverte estado
                analisando = !analisando;
                memset(buf, 0, SSD1306_BUF_LEN);
                WriteString(buf, 5, 24, "audio gravado");
                WriteString(buf, 5, 40, "processando...");
                render(buf, &frame_area);

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
