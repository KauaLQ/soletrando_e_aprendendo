// matriz_led/neopixel_pio.h

#ifndef NEOPIXEL_PIO_H
#define NEOPIXEL_PIO_H

#include "pico/stdlib.h"

void npInit(uint pin);
void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b);
void npClear(void);
void npWrite(void);
void npWriteTen(void);
void npWriteNine(void);
void npWriteEigth(void);
void npWriteSeven(void);
void npWriteSix(void);
void npWriteFive(void);
void npWriteFour(void);
void npWriteThree(void);
void npWriteTwo(void);
void npWriteOne(void);
void npWriteZero(void);
void npWriteLeft(void);
void npWriteRigth(void);
void npWriteFace(void);
void npWriteX(void);

#endif