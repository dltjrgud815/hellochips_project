// external.c
#include "external.h"   // 헤더 파일 포함
#include <debug.h>
#include <sal_api.h>
#include <main.h>
#include <stdint.h>
#include <gpio.h>
#include <timer.h>
#include <timer_test.h>
#include <ipc.h>
#define TRIGGER_PIN GPIO_GPG(6)  // 트리거 핀 (예: GPIO_GPG(6))
#define ECHO_PIN    GPIO_GPG(7)  // 에코 핀 (예: GPIO_GPG(7))
#define SOUND_SPEED_CM_PER_US 0.0343  // 음속 (cm/µs)
/////////////////////<Declaration>///////////////////////
static void Ultra_Test(void *pArg);
static uint32 calculate_distance(uint32 duration_ticks);
static uint32 ultrasonic_read_distance(void) ;
SALRetCode_t CreateQueue(void);
void EnqueueData(uint32 data);
/////////////////////<Function>///////////////////////
static uint32 calculate_distance(uint32 duration_ticks) {
    // 클럭 값을 마이크로초로 변환 후 거리 계산
    uint32 duration_us = duration_ticks;
    return (duration_us * SOUND_SPEED_CM_PER_US) / 2.0;  // 왕복 거리이므로 나누기 2
}

static uint32 ultrasonic_read_distance() {
    timer_on();
    uint32 start_time = 0;
    uint32 end_time = 0;
    uint32 dist;
    
    //GPIO_Set도 상관X -> Test하느라 Write로 진행한거
    GPIO_Set(TRIGGER_PIN, 0UL);
    SAL_TaskSleep(50);
    GPIO_Set(TRIGGER_PIN, 1UL);
    // 트리거 신호 (10μs 동안 HIGH)
    delay_us3(10);
    GPIO_Set(TRIGGER_PIN, 0UL);
 
   
    while (GPIO_Get(ECHO_PIN) == 0UL);
    start_time = TIMER_GetCurrentMainCounter();  // 타이머 값 읽기

    // 에코 핀이 LOW가 될 때까지 대기 (end_time 기록)
    while (GPIO_Get(ECHO_PIN) == 1UL);
    end_time = TIMER_GetCurrentMainCounter();    // 타이머 값 읽기
    
    uint32 duration_us = end_time - start_time;
    TIMER_Disable(TIMER_CH_2);
    dist = calculate_distance(duration_us);

    // 거리 계산
    //mcu_printf("dura : 0x%08X\n",duration_us);
    //mcu_printf("tick: %d\n",duration_us);
    //mcu_printf("dist : %d cm\n",(calculate_distance(duration_us)));

    return dist;
}

static void Ultra_Test(void *pArg)
{
    (void)pArg;
    uint32 distance;

    mcu_printf("ultra test start\n");
    
    //ultrasonic_init();
    GPIO_Config(GPIO_GPG(6), (GPIO_FUNC(0UL) | GPIO_OUTPUT));
    // 입력 핀 설정
    GPIO_Config(GPIO_GPG(7), (GPIO_FUNC(0UL) | GPIO_INPUT | GPIO_INPUTBUF_EN));    
    
    mcu_printf("before while(1)\n");
    while (1) {        
        distance = ultrasonic_read_distance();
	//mcu_printf("dist : %d cm\n",distance);
	EnqueueData(distance);
	//(void)IPC_SendPacket(3, 5, 1, NULL, 1);
        SAL_TaskSleep(300);
    }
}
SALRetCode_t CreateQueue(void) {
    //uint32 depth = 10;             // Queue에 저장할 최대 데이터 개수
   

    SALRetCode_t result = SAL_QueueCreate(&myQueueId, 
                                        (const uint8 *)"MyQueue", 
                                        1UL, 
                                        sizeof(uint32));

    if (result == SAL_RET_SUCCESS) {
        mcu_printf("Queue created successfully. ID: %d\n", myQueueId);
    } else {
        mcu_printf("Failed to create queue\n");
    }

    return result;
}
void EnqueueData(uint32 data) {
    (void)SAL_QueuePut(myQueueId, &data, sizeof(data), 0, SAL_OPT_NON_BLOCKING);
	/*
    }*/

}
void LED_TEST_Task(void) //Ultrasonic임 이름만 아바꿈
{   
    mcu_printf("!!@#Ultra task test !!!@#@\n");
    
    static uint32   LED_TaskID;
    static uint32   LED_TEST_Stk[LED_TEST_STK_SIZE];
	if (CreateQueue() != SAL_RET_SUCCESS) {
        mcu_printf("Failed to initialize queue\n");
        return;
    }
    (void)SAL_TaskCreate
    (
        &LED_TaskID,
	(const uint8 *)"LED Task",
        (SALTaskFunc)&Ultra_Test,
        &LED_TEST_Stk[0],
        LED_TEST_STK_SIZE,
        8,
        NULL
    );
}

