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

uint32 weighted_moving_average_filter(UltrasonicFilterState *state, uint32 new_distance);
void initialize_filter(UltrasonicFilterState *state);
/////////////////////<Function>///////////////////////
static uint32 calculate_distance(uint32 duration_ticks) {
    // 클럭 값을 마이크로초로 변환 후 거리 계산
    uint32 duration_us = duration_ticks;
    return (duration_us * SOUND_SPEED_CM_PER_US) / 2.0;  // 왕복 거리이므로 나누기 2
}

static uint32 ultrasonic_read_distance(UltrasonicFilterState *filter_state) {
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
 
    uint32 timeout_start = TIMER_GetCurrentMainCounter();
    while (GPIO_Get(ECHO_PIN) == 0UL){
        if ((TIMER_GetCurrentMainCounter() - timeout_start) > TIMEOUT_US) {
            //mcu_printf("Timeout waiting for ECHO LOW\n");
            return; // 타임아웃 발생 시 종료
        }
    }
    start_time = TIMER_GetCurrentMainCounter();  // 타이머 값 읽기

    // 에코 핀이 LOW가 될 때까지 대기 (end_time 기록)
    timeout_start = TIMER_GetCurrentMainCounter();
    while (GPIO_Get(ECHO_PIN) == 1UL){
        if ((TIMER_GetCurrentMainCounter() - timeout_start) > TIMEOUT_US) {
            //mcu_printf("Timeout waiting for ECHO LOW\n");
            return; // 타임아웃 발생 시 종료
        }
    }
    end_time = TIMER_GetCurrentMainCounter();    // 타이머 값 읽기
    
    uint32 duration_us = end_time - start_time;
    TIMER_Disable(TIMER_CH_2);

    dist = 400;
    uint32 raw_data = calculate_distance(duration_us);
    if(raw_data > 0 && raw_data < 400){
        dist = weighted_moving_average_filter(filter_state, raw_data);
        // 거리 계산
    }
    else return;
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
//가중 이동 평균 필터
uint32 weighted_moving_average_filter(UltrasonicFilterState *state, uint32 new_distance) {
    //const uint32 max_deviation = 400; // 허용 가능한 최대 편차
    const uint32 weights[3] = {5, 3, 2}; // 최신 값부터 과거 값에 부여할 가중치

    // 새 값을 배열에 저장
    state->distances[state->index] = new_distance;
    state->index = (state->index + 1) % 3;

    // 가중 평균 계산
    uint32 weighted_sum = 0;
    uint32 total_weight = 0;
    for (uint32 i = 0; i < 3; i++) {
        // 최신 데이터를 weights[0]에 매핑
        uint32 circular_index = (state->index + 3 - i) % 3; // 역순으로 인덱스 계산
        weighted_sum += state->distances[circular_index] * weights[i];
        total_weight += weights[i];
    }

    return weighted_sum / total_weight;
}
//필터 초기화
void initialize_filter(UltrasonicFilterState *state) {
    for (uint32 i = 0; i < 3; i++) {
        state->distances[i] = 0; // 초기화
    }
    state->index = 0;
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

