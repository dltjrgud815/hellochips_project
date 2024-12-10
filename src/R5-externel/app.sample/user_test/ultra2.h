// ultra2.h
#ifndef EXTERNAL_ULTRA2_H
#define EXTERNAL_ULTRA2_H

#define ULTRA2_STK_SIZE               (512UL)
#include <sal_internal.h>

//void externalFunction(void);
//static void ConsoleTask(void *pArg);
void Ultra2_Task(void);
// 필터링 구조체
typedef struct {
    uint32 distances[3]; // 최근 3개의 거리 값 저장
    uint32 index;        // 현재 배열 인덱스
} UltrasonicFilterState2;
#endif // EXTERNAL_H

