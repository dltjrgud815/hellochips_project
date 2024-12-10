// external.h
#ifndef EXTERNAL_H
#define EXTERNAL_H

#define LED_TEST_STK_SIZE               (512UL)
#include <sal_internal.h>

//void externalFunction(void);
//static void ConsoleTask(void *pArg);
void LED_TEST_Task(void);
typedef struct {
    uint32 distances[3]; // 최근 3개의 거리 값 저장
    uint32 index;        // 현재 배열 인덱스
} UltrasonicFilterState;
#endif // EXTERNAL_H
