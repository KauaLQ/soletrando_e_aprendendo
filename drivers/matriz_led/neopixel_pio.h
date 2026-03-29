// matriz_led/neopixel_pio.h

#ifndef NEOPIXEL_PIO_H
#define NEOPIXEL_PIO_H

#include "pico/stdlib.h"

// Estrutura para armazenar LEDs e cor
typedef struct {
    const uint8_t* leds;
    uint8_t count;
    uint8_t r, g, b;
} DigitPattern;

// Função para desenhar o número
void npWriteNumber(uint8_t number);

void npInit(uint pin);
void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b);
void npClear(void);
void npWrite(void);
void npWriteLeft(void);
void npWriteRigth(void);
void npWriteFace(void);
void npWriteX(void);
void npWriteV(void);

#endif