// dht11.c
#include "dht11.h"   // 헤더 파일 포함
#include <debug.h>
#include <sal_api.h>
#include <main.h>
#include <gpio.h>
#include <timer.h>
#include <timer_test.h>
#include <ipc.h>

#define DHT_PIN GPIO_GPG(10)  // 트리거 핀 
#define MAX_TIMINGS 85       // DHT11의 최대 신호 타이밍

static void read_dht11_data(void);
//SALRetCode_t DequeueData(void);

/************************Function*********************/
int dht_data[5] = {0};
uint32 receivedData;
// GPIO 핀을 읽어 신호의 상태를 기록
static void read_dht11_data(void) {
    uint8 last_state = 1;
    uint8 counter = 0;
    uint8 j = 0, i;
    timer_on();
    // 데이터 초기화
    dht_data[0] = dht_data[1] = dht_data[2] = dht_data[3] = dht_data[4] = 0; 

    // DHT11에 신호 전송: LOW 18ms, HIGH 20~40us
    GPIO_Config(DHT_PIN, (GPIO_FUNC(0UL) | GPIO_OUTPUT));
    GPIO_Set(DHT_PIN, 1UL);
    SAL_TaskSleep(1);
    GPIO_Set(DHT_PIN, 0UL);
    SAL_TaskSleep(18);  // 18ms 대기
    GPIO_Set(DHT_PIN, 1UL);
    delay_us3(20);  // 20~40us 대기
   
    GPIO_Config(GPIO_GPG(10), (GPIO_FUNC(0UL) | GPIO_INPUT | GPIO_INPUTBUF_EN));  
    
    for (i = 0; i < MAX_TIMINGS; i++) {
        counter = 0;
        while (GPIO_Get(DHT_PIN) == last_state) {
            counter++;
            delay_us3(1);
            if (counter == 255) break;
        }
    //mcu_printf("GPIO : %d\n counter : %d\n",GPIO_Get(DHT_PIN),counter);
        last_state = GPIO_Get(DHT_PIN);

        if (counter == 255) break;

        // 첫 번째 3 비트는 무시 (시작 신호)
        if ((i >= 4) && (i % 2 == 0)) {
            dht_data[j / 8] <<= 1;
            if (counter > 30) dht_data[j / 8] |= 1; 
            //온습도 센서가 몇 us동안 High를 유지했는지에 따라 달라짐
            //0 : 26~28us, 1 : 70us (counter로 구분지음)
            j++;
        }
    }
    //mcu_printf("i: %d j : %dstart!!!@\n",i, j);
    //mcu_printf("humidity : %d.%d%%\n temp : %d.%d\n\n",dht_data[0],dht_data[1],dht_data[2],dht_data[3],dht_data[4]);
    //mcu_printf("done\n");

    // 체크섬 확인
    if ((j >= 40) && (dht_data[4] == ((dht_data[0] + dht_data[1] + dht_data[2] + dht_data[3]) & 0xFF))) {
        //mcu_printf("done\n");
        if(dht_data[0] > 60 || dht_data[2] > 27){
            (void)IPC_SendPacket(3, 1, 1, NULL, 1);
        }
        else{
            (void)IPC_SendPacket(3, 1, 1, NULL, 0);
        }
        
    } else {
        
    }
    TIMER_Disable(TIMER_CH_2);  

}

static void DHT_operate(void *pArg)
{
    (void)pArg;

    while (1) { 
        read_dht11_data();
        SAL_TaskSleep(500); // 1초 대기
    }
}

void DHT_Task(void) //DHT task
{   
    //mcu_printf("!!@#DHT task start !!!@#@\n");
    
    static uint32   DHT_TaskID;
    static uint32   DHT_Stk[DHT_STK_SIZE];

    (void)SAL_TaskCreate
    (
        &DHT_TaskID,
        (const uint8 *)"DHT operation Task",
        (SALTaskFunc)&DHT_operate,
        &DHT_Stk[0],
        DHT_STK_SIZE,
        SAL_PRIO_DHT,
        NULL
    );
}