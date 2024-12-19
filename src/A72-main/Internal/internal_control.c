#include "internal_control.h"

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

// GPIO 핀의 방향을 설정 (in)
void set_gpio_direction_in(const char *gpio_pin) {
    char path[50];
    sprintf(path, "/sys/class/gpio/gpio%s/direction", gpio_pin);
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("Failed to set GPIO direction");
        exit(1);
    }
    fprintf(fp, "in");
    fclose(fp);
}

// GPIO 핀의 값을 읽음 (버튼 상태 확인)
int get_gpio_value(const char *gpio_pin) {
    char path[50], value_str[3];
    sprintf(path, "/sys/class/gpio/gpio%s/value", gpio_pin);
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror("Failed to get GPIO value");
        exit(1);
    }
    fgets(value_str, 3, fp);
    fclose(fp);
    return atoi(value_str);
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
void turn_on_light() {
    // GPIO 84번, 85번, 86번 핀 설정
    export_gpio(GPIO_89);
    export_gpio(GPIO_90);
    export_gpio(GPIO_65);

    set_gpio_direction(GPIO_89);
    set_gpio_direction(GPIO_90);
    set_gpio_direction(GPIO_65);

    // GPIO 84번은 1, 85번은 0, 86번은 1로 설정
    set_gpio_value(GPIO_89, 1);  // GPIO 84번 HIGH
    set_gpio_value(GPIO_90, 0);  // GPIO 85번 LOW
    set_gpio_value(GPIO_65, 1);  // GPIO 86번 HIGH
}

// GPIO 핀을 제어하는 함수 (발광 및 끄기)
void turn_on_fan() {
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
void turn_off_light() {
    // 종료 시 모든 GPIO 값 0으로 설정
    set_gpio_value(GPIO_65, 0);  // GPIO 86번 LOW
    set_gpio_value(GPIO_89, 0);  // GPIO 84번 LOW
    set_gpio_value(GPIO_90, 0);  // GPIO 85번 LOW
}

// GPIO 핀을 종료할 때 모든 핀을 LOW로 설정하고 unexport 처리
void turn_off_fan() {
    // 종료 시 모든 GPIO 값 0으로 설정
    set_gpio_value(GPIO_84, 0);  // GPIO 84번 LOW
    set_gpio_value(GPIO_85, 0);  // GPIO 85번 LOW
    set_gpio_value(GPIO_86, 0);  // GPIO 86번 LOW
}
