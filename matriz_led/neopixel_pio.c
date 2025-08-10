// matriz_led/neopixel_pio.c

#include "neopixel_pio.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2818b.pio.h"

#define LED_COUNT 25
#define LED_PIN 7

// Definição de pixel GRB
struct pixel_t {
  uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

// Declaração do buffer de pixels que formam a matriz.
npLED_t leds[LED_COUNT];

// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;

/**
 * Inicializa a máquina PIO para controle da matriz de LEDs.
 */
void npInit(uint pin) {

  // Cria programa PIO.
  uint offset = pio_add_program(pio0, &ws2818b_program);
  np_pio = pio0;

  // Toma posse de uma máquina PIO.
  sm = pio_claim_unused_sm(np_pio, false);
  if (sm < 0) {
    np_pio = pio1;
    sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
  }

  // Inicia programa na máquina PIO obtida.
  ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);

  // Limpa buffer de pixels.
  for (uint i = 0; i < LED_COUNT; ++i) {
    leds[i].R = 0;
    leds[i].G = 0;
    leds[i].B = 0;
  }
}

/**
 * Atribui uma cor RGB a um LED.
 */
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
  leds[index].R = r;
  leds[index].G = g;
  leds[index].B = b;
}

/**
 * Limpa o buffer de pixels.
 */
void npClear() {
  for (uint i = 0; i < LED_COUNT; ++i)
    npSetLED(i, 0, 0, 0);
}

/**
 * Escreve os dados do buffer nos LEDs.
 */
void npWrite() {
  // Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
  for (uint i = 0; i < LED_COUNT; ++i) {
    pio_sm_put_blocking(np_pio, sm, leds[i].G);
    pio_sm_put_blocking(np_pio, sm, leds[i].R);
    pio_sm_put_blocking(np_pio, sm, leds[i].B);
  }
  sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

void npWriteTen(){
  npClear();

  npSetLED(24, 0, 80, 0);
  npSetLED(15, 0, 80, 0);
  npSetLED(14, 0, 80, 0);
  npSetLED(5, 0, 80, 0);
  npSetLED(4, 0, 80, 0);

  npSetLED(22, 0, 80, 0);
  npSetLED(21, 0, 80, 0);
  npSetLED(20, 0, 80, 0);
  npSetLED(24, 0, 80, 0);
  npSetLED(17, 0, 80, 0);
  npSetLED(19, 0, 80, 0);
  npSetLED(12, 0, 80, 0);
  npSetLED(10, 0, 80, 0);
  npSetLED(7, 0, 80, 0);
  npSetLED(9, 0, 80, 0);
  npSetLED(0, 0, 80, 0);
  npSetLED(1, 0, 80, 0);
  npSetLED(2, 0, 80, 0);

  npWrite();
}

void npWriteNine(){
  npClear();

  npSetLED(23, 0, 80, 0);
  npSetLED(22, 0, 80, 0);
  npSetLED(21, 0, 80, 0);
  npSetLED(16, 0, 80, 0);
  npSetLED(18, 0, 80, 0);
  npSetLED(13, 0, 80, 0);
  npSetLED(12, 0, 80, 0);
  npSetLED(11, 0, 80, 0);
  npSetLED(8, 0, 80, 0);
  npSetLED(1, 0, 80, 0);
  npSetLED(2, 0, 80, 0);
  npSetLED(3, 0, 80, 0);

  npWrite();
}

void npWriteEigth(){
  npClear();

  npSetLED(23, 0, 80, 0);
  npSetLED(22, 0, 80, 0);
  npSetLED(21, 0, 80, 0);
  npSetLED(16, 0, 80, 0);
  npSetLED(18, 0, 80, 0);
  npSetLED(13, 0, 80, 0);
  npSetLED(12, 0, 80, 0);
  npSetLED(11, 0, 80, 0);
  npSetLED(6, 0, 80, 0);
  npSetLED(8, 0, 80, 0);
  npSetLED(1, 0, 80, 0);
  npSetLED(2, 0, 80, 0);
  npSetLED(3, 0, 80, 0);

  npWrite();
}

void npWriteSeven(){
  npClear();

  npSetLED(23, 0, 80, 0);
  npSetLED(22, 0, 80, 0);
  npSetLED(21, 0, 80, 0);
  npSetLED(18, 0, 80, 0);
  npSetLED(11, 0, 80, 0);
  npSetLED(8, 0, 80, 0);
  npSetLED(1, 0, 80, 0);

  npWrite();
}

void npWriteSix(){
  npClear();

  npSetLED(23, 0, 80, 0);
  npSetLED(22, 0, 80, 0);
  npSetLED(21, 0, 80, 0);
  npSetLED(16, 0, 80, 0);
  npSetLED(13, 0, 80, 0);
  npSetLED(12, 0, 80, 0);
  npSetLED(11, 0, 80, 0);
  npSetLED(6, 0, 80, 0);
  npSetLED(8, 0, 80, 0);
  npSetLED(1, 0, 80, 0);
  npSetLED(2, 0, 80, 0);
  npSetLED(3, 0, 80, 0);

  npWrite();
}

void npWriteFive(){
  npClear();

  npSetLED(23, 80, 0, 0);
  npSetLED(22, 80, 0, 0);
  npSetLED(21, 80, 0, 0);
  npSetLED(16, 80, 0, 0);
  npSetLED(13, 80, 0, 0);
  npSetLED(12, 80, 0, 0);
  npSetLED(11, 80, 0, 0);
  npSetLED(8, 80, 0, 0);
  npSetLED(1, 80, 0, 0);
  npSetLED(2, 80, 0, 0);
  npSetLED(3, 80, 0, 0);

  npWrite();
}

void npWriteFour(){
  npClear();

  npSetLED(23, 80, 0, 0);
  npSetLED(21, 80, 0, 0);
  npSetLED(18, 80, 0, 0);
  npSetLED(16, 80, 0, 0);
  npSetLED(13, 80, 0, 0);
  npSetLED(12, 80, 0, 0);
  npSetLED(11, 80, 0, 0);
  npSetLED(8, 80, 0, 0);
  npSetLED(1, 80, 0, 0);

  npWrite();
}

void npWriteThree(){
  npClear();

  npSetLED(23, 80, 0, 0);
  npSetLED(22, 80, 0, 0);
  npSetLED(21, 80, 0, 0);
  npSetLED(18, 80, 0, 0);
  npSetLED(13, 80, 0, 0);
  npSetLED(12, 80, 0, 0);
  npSetLED(11, 80, 0, 0);
  npSetLED(8, 80, 0, 0);
  npSetLED(1, 80, 0, 0);
  npSetLED(2, 80, 0, 0);
  npSetLED(3, 80, 0, 0);

  npWrite();
}

void npWriteTwo(){
  npClear();

  npSetLED(23, 80, 0, 0);
  npSetLED(22, 80, 0, 0);
  npSetLED(21, 80, 0, 0);
  npSetLED(18, 80, 0, 0);
  npSetLED(13, 80, 0, 0);
  npSetLED(12, 80, 0, 0);
  npSetLED(11, 80, 0, 0);
  npSetLED(6, 80, 0, 0);
  npSetLED(1, 80, 0, 0);
  npSetLED(2, 80, 0, 0);
  npSetLED(3, 80, 0, 0);

  npWrite();
}

void npWriteOne(){
  npClear();

  npSetLED(21, 80, 0, 0);
  npSetLED(18, 80, 0, 0);
  npSetLED(11, 80, 0, 0);
  npSetLED(8, 80, 0, 0);
  npSetLED(1, 80, 0, 0);

  npWrite();
}

void npWriteZero(){
  npClear();

  npSetLED(23, 80, 0, 0);
  npSetLED(22, 80, 0, 0);
  npSetLED(21, 80, 0, 0);
  npSetLED(18, 80, 0, 0);
  npSetLED(16, 80, 0, 0);
  npSetLED(13, 80, 0, 0);
  npSetLED(11, 80, 0, 0);
  npSetLED(8, 80, 0, 0);
  npSetLED(6, 80, 0, 0);
  npSetLED(1, 80, 0, 0);
  npSetLED(2, 80, 0, 0);
  npSetLED(3, 80, 0, 0);

  npWrite();
}

void npWriteLeft(){
  npClear();

  npSetLED(22, 0, 0, 80);
  npSetLED(16, 0, 0, 80);
  npSetLED(17, 0, 0, 80);
  npSetLED(14, 0, 0, 80);
  npSetLED(13, 0, 0, 80);
  npSetLED(12, 0, 0, 80);
  npSetLED(11, 0, 0, 80);
  npSetLED(10, 0, 0, 80);
  npSetLED(6, 0, 0, 80);
  npSetLED(7, 0, 0, 80);
  npSetLED(2, 0, 0, 80);

  npWrite();
}

void npWriteRigth(){
  npClear();

  npSetLED(22, 0, 0, 80);
  npSetLED(18, 0, 0, 80);
  npSetLED(17, 0, 0, 80);
  npSetLED(14, 0, 0, 80);
  npSetLED(13, 0, 0, 80);
  npSetLED(12, 0, 0, 80);
  npSetLED(11, 0, 0, 80);
  npSetLED(10, 0, 0, 80);
  npSetLED(8, 0, 0, 80);
  npSetLED(7, 0, 0, 80);
  npSetLED(2, 0, 0, 80);

  npWrite();
}

void npWriteFace(){
  npClear();

  npSetLED(16, 0, 0, 80);
  npSetLED(18, 0, 0, 80);
  npSetLED(13, 0, 0, 80);
  npSetLED(11, 0, 0, 80);
  npSetLED(5, 0, 0, 80);
  npSetLED(9, 0, 0, 80);
  npSetLED(1, 0, 0, 80);
  npSetLED(2, 0, 0, 80);
  npSetLED(3, 0, 0, 80);

  npWrite();
}

void npWriteX(){
  npClear();

  npSetLED(24, 80, 0, 0);
  npSetLED(20, 80, 0, 0);
  npSetLED(16, 80, 0, 0);
  npSetLED(18, 80, 0, 0);
  npSetLED(12, 80, 0, 0);
  npSetLED(8, 80, 0, 0);
  npSetLED(6, 80, 0, 0);
  npSetLED(4, 80, 0, 0);
  npSetLED(0, 80, 0, 0);

  npWrite();
}

void npWriteV(){
  npClear();

  npSetLED(19, 0, 80, 0);
  npSetLED(11, 0, 80, 0);
  npSetLED(7, 0, 80, 0);
  npSetLED(5, 0, 80, 0);
  npSetLED(3, 0, 80, 0);

  npWrite();
}