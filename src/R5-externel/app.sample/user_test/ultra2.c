// ultra2.c
#include "ultra2.h"   // 헤더 파일 포함
#include <debug.h>
#include <sal_api.h>
#include <main.h>
#include <stdint.h>
#include <gpio.h>
#include <timer.h>
#include <timer_test.h>
#define TRIGGER_PIN GPIO_GPG(2)  // 트리거 핀 
#define ECHO_PIN    GPIO_GPG(10)  // 에코 핀 
#define SOUND_SPEED_CM_PER_US 0.0343  // 음속 (cm/µs)

/////////////////////<Declaration>///////////////////////

static void Ultra2_operate(void *pArg);
static uint32 calculate_distance(uint32 duration_ticks);
static void ultrasonic_read_distance(void) ;

/////////////////////<Function>///////////////////////

static uint32 calculate_distance(uint32 duration_ticks) {
    // 클럭 값을 마이크로초로 변환 후 거리 계산
    uint32 duration_us = duration_ticks;
    return (duration_us * SOUND_SPEED_CM_PER_US) / 2.0;  // 왕복 거리이므로 나누기 2
}

static void ultrasonic_read_distance() {
    
    uint32 start_time = 0;
    uint32 end_time = 0;
    
    
    GPIO_Set(TRIGGER_PIN, 0UL);
    SAL_TaskSleep(50);
    // 트리거 신호 (10μs 동안 HIGH)
    GPIO_Set(TRIGGER_PIN, 1UL);
    delay_us3(10);
   
    GPIO_Set(TRIGGER_PIN, 0UL);
     
    while (GPIO_Get(ECHO_PIN) == 0UL);
    start_time = TIMER_GetCurrentMainCounter();  // 타이머 값 읽기

    // 에코 핀이 LOW가 될 때까지 대기 (end_time 기록)
    while (GPIO_Get(ECHO_PIN) == 1UL);
    end_time = TIMER_GetCurrentMainCounter();    // 타이머 값 읽기
    
    uint32 duration_us = end_time - start_time;
    
    uint32 dist_data = calculate_distance(duration_us);
    // 거리 계산
    //mcu_printf("dura : 0x%08X\n",duration_us);
    //mcu_printf("tick: %d\n",duration_us);
    mcu_printf("dist2 : %d cm\n",dist_data);
    //if((dist_data < 10)&&(dist_data > 0)) EnqueueData(dist_data);
}

static void Ultra2_operate(void *pArg)
{
    (void)pArg;
    mcu_printf("ultra2 test start\n");
    
    //ultrasonic_init();
    GPIO_Config(TRIGGER_PIN, (GPIO_FUNC(0UL) | GPIO_OUTPUT));
    // 입력 핀 설정
    GPIO_Config(ECHO_PIN, (GPIO_FUNC(0UL) | GPIO_INPUT | GPIO_INPUTBUF_EN));    
    
    while (1) {        
        ultrasonic_read_distance();
        SAL_TaskSleep(1000); // 1초 대기
    }
}



void Ultra2_Task(void) //DHT TEST 임 이름만 아바꿈
{   
    mcu_printf("!!@#Ultra2 task test !!!@#@\n");
    
    static uint32   ULTRA2_TaskID;
    static uint32   ULTRA2_Stk[ULTRA2_STK_SIZE];
  
    (void)SAL_TaskCreate
    (
        &ULTRA2_TaskID,
        (const uint8 *)"Ultra2 Task",
        (SALTaskFunc)&Ultra2_operate,
        &ULTRA2_Stk[0],
        ULTRA2_STK_SIZE,
        SAL_PRIO_TEST,
        NULL
    );
}