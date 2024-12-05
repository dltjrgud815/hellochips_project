#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define GPIO_84 "84"
#define GPIO_85 "85"
#define GPIO_86 "86"

// GPIO를 export하여 사용 가능하도록 설정
void export_gpio(const char *gpio_pin) {
    FILE *fp = fopen("/sys/class/gpio/export", "w");
    if (fp == NULL) {
        perror("Failed to export GPIO");
        exit(1);
    }
    fprintf(fp, "%s", gpio_pin);
    fclose(fp);
}

// GPIO 핀의 방향을 설정 (출력)
void set_gpio_direction(const char *gpio_pin) {
    char path[50];
    sprintf(path, "/sys/class/gpio/gpio%s/direction", gpio_pin);
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("Failed to set GPIO direction");
        exit(1);
    }
    fprintf(fp, "out");
    fclose(fp);
}

// GPIO 핀에 값을 설정 (HIGH 또는 LOW)
void set_gpio_value(const char *gpio_pin, int value) {
    char path[50];
    sprintf(path, "/sys/class/gpio/gpio%s/value", gpio_pin);
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("Failed to set GPIO value");
        exit(1);
    }
    fprintf(fp, "%d", value);
    fclose(fp);
}

// GPIO를 unexport하여 해제
void unexport_gpio(const char *gpio_pin) {
    FILE *fp = fopen("/sys/class/gpio/unexport", "w");
    if (fp == NULL) {
        perror("Failed to unexport GPIO");
        exit(1);
    }
    fprintf(fp, "%s", gpio_pin);
    fclose(fp);
}

// GPIO 핀을 제어하는 함수 (발광 및 끄기)
void control_PAN() {
    // GPIO 84번, 85번, 86번 핀 설정
    export_gpio(GPIO_84);
    export_gpio(GPIO_85);
    export_gpio(GPIO_86);

    set_gpio_direction(GPIO_84);
    set_gpio_direction(GPIO_85);
    set_gpio_direction(GPIO_86);

    // GPIO 84번은 1, 85번은 0, 86번은 1로 설정
    set_gpio_value(GPIO_84, 1);  // GPIO 84번 HIGH
    set_gpio_value(GPIO_85, 0);  // GPIO 85번 LOW
    set_gpio_value(GPIO_86, 1);  // GPIO 86번 HIGH
}

// GPIO 핀을 종료할 때 모든 핀을 LOW로 설정하고 unexport 처리
void stop_PAN() {
    // 종료 시 모든 GPIO 값 0으로 설정
    set_gpio_value(GPIO_84, 0);  // GPIO 84번 LOW
    set_gpio_value(GPIO_85, 0);  // GPIO 85번 LOW
    set_gpio_value(GPIO_86, 0);  // GPIO 86번 LOW

    // 프로그램 종료 시 모든 GPIO unexport
//    set_gpio_direction(GPIO_84);
//    set_gpio_direction(GPIO_85);
//    set_gpio_direction(GPIO_86);
}

/*
int main() {
    // GPIO 제어 함수 호출
    control_PAN();

    // 잠시 대기 후 stop_gpio 호출하여 종료
    sleep(10);  // 10초 동안 GPIO 제어
    stop_PAN();

    return 0;
}

*/
