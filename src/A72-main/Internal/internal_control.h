#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define GPIO_65 "65"
#define GPIO_84 "84"
#define GPIO_85 "85"
#define GPIO_86 "86"
#define GPIO_89 "89"
#define GPIO_90 "90"
#define GPIO_113 "113"

// 함수 선언
void export_gpio(const char *gpio_pin);
void set_gpio_direction(const char *gpio_pin);
void set_gpio_direction_in(const char *gpio_pin);
int get_gpio_value(const char *gpio_pin);
void set_gpio_value(const char *gpio_pin, int value);
void unexport_gpio(const char *gpio_pin);
void turn_on_light();
void turn_on_fan();
void turn_off_light();
void turn_off_fan();
