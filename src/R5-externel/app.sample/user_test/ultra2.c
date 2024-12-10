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
#define TIMEOUT_US 20000 // 타임아웃 기준 (20ms)
/////////////////////<Declaration>///////////////////////

static void Ultra2_operate(void *pArg);
static uint32 calculate_distance(uint32 duration_ticks);
static void ultrasonic_read_distance(UltrasonicFilterState2 *filter_state) ;

static uint32 weighted_moving_average_filter(UltrasonicFilterState2 *state, uint32 new_distance);
static void initialize_filter(UltrasonicFilterState2 *state);
/////////////////////<Function>///////////////////////

static uint32 calculate_distance(uint32 duration_ticks) {
    // 클럭 값을 마이크로초로 변환 후 거리 계산
    uint32 duration_us = duration_ticks;
    return (duration_us * SOUND_SPEED_CM_PER_US) / 2.0;  // 왕복 거리이므로 나누기 2
}

static void ultrasonic_read_distance(UltrasonicFilterState2 *filter_state) {
    
    uint32 start_time = 0;
    uint32 end_time = 0;
    
    
    GPIO_Set(TRIGGER_PIN, 0UL);
    SAL_TaskSleep(50);
    // 트리거 신호 (10μs 동안 HIGH)
    GPIO_Set(TRIGGER_PIN, 1UL);
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
    uint32 dist_data = 400;
    //TIMER_Disable(TIMER_CH_2);  
    uint32 raw_data = calculate_distance(duration_us);
    if(raw_data > 0 && raw_data < 400){
        dist_data = weighted_moving_average_filter(filter_state, raw_data);
        // 거리 계산
    }
    else return;
    mcu_printf("dist2 : %d cm\n",dist_data);
}

static void Ultra2_operate(void *pArg)
{
    (void)pArg;
    mcu_printf("ultra2 test start\n");
    UltrasonicFilterState2 filter_state;
    initialize_filter(&filter_state);
    //ultrasonic_init();
    GPIO_Config(TRIGGER_PIN, (GPIO_FUNC(0UL) | GPIO_OUTPUT));
    // 입력 핀 설정
    GPIO_Config(ECHO_PIN, (GPIO_FUNC(0UL) | GPIO_INPUT | GPIO_INPUTBUF_EN));    
    
    while (1) {        
        ultrasonic_read_distance(&filter_state);
        SAL_TaskSleep(50); // 1초 대기
    }
}


static uint32 weighted_moving_average_filter(UltrasonicFilterState2 *state, uint32 new_distance) {
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
static void initialize_filter(UltrasonicFilterState2 *state) {
    for (uint32 i = 0; i < 3; i++) {
        state->distances[i] = 0; // 초기화
    }
    state->index = 0;
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