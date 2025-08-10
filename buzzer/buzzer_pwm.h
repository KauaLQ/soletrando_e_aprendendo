#ifndef BUZZER_PWM_H
#define BUZZER_PWM_H

#include "pico/stdlib.h"

void pwm_init_buzzer(uint pin);
void play_tone(uint pin, uint frequency, uint duration_ms);
void play_star_wars(uint pin);
void beep(uint pin, uint freq_hz, uint duration_ms);

#endif