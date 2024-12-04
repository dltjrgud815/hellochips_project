// light.c
#include "light.h"   // 헤더 파일 포함
#include <debug.h>
#include <sal_api.h>
#include <main.h>
#include <gpio.h>
#include <timer.h>
#include <timer_test.h>
#include <ipc.h>


/**********************Declaration*************************/
static uint32 g_detected_s_addr = 0;
void I2C_Light(void);
void I2C_YL40_ReadData(uint8 channel);
/**********************Function****************************/

void I2C_YL40_ReadData(uint8 channel) {
    uint8 read_data[1] = {0}; // 조도 데이터 저장 버퍼
    uint8 dummy[1] = {0x40};
    g_detected_s_addr = I2C_ScanSlave(channel);
    
    if (I2C_Xfer(channel, ((uint8)(g_detected_s_addr << 1)), 
                             (uint8)1UL, &dummy[0] ,  
                             (uint8)1UL, &read_data[0], 
                             NULL, NULL
                             ) != SAL_RET_SUCCESS) {
        //mcu_printf("[I2C] Failed to set channel2 %d\n", channel);
    }
    
    uint8 lux = read_data[0]; 
    //mcu_printf("YL-40 Lux Value: %d\n", lux);
    if(lux < 120){
        (void)IPC_SendPacket(3, 2, 1, NULL, 1);
    }
    else{
        (void)IPC_SendPacket(3, 2, 1, NULL, 0);
    }
}

void I2C_Light(void) {
    uint8 i2c_channel = (uint8)1UL; // 사용할 I2C 채널
    SALRetCode_t ret;
    ret = SAL_RET_SUCCESS;

    // I2C 초기화
    ret = I2C_Open(i2c_channel, GPIO_GPC(20), GPIO_GPC(21), I2C_TEST_CLK_RATE_100, NULL, NULL);
     if(ret != SAL_RET_SUCCESS)
    {
        //mcu_printf("open %s fail line %d\n", __func__ ,__LINE__);
    }

    // YL-40 데이터 읽기
    while (1) {
        I2C_YL40_ReadData(i2c_channel);
        SAL_TaskSleep(500); // 1초 간격으로 읽기
    }

    // I2C 종료
    I2C_Close(i2c_channel);
}


void LIGHT_Task(void) //DHT task
{   
    //mcu_printf("!!@#Light task start !!!@#@\n");
    
    static uint32   LIGHT_TaskID;
    static uint32   DHT_Stk[LIGHT_STK_SIZE];

    (void)SAL_TaskCreate
    (
        &LIGHT_TaskID,
        (const uint8 *)"LIGHT operation Task",
        (SALTaskFunc)&I2C_Light,
        &DHT_Stk[0],
        LIGHT_STK_SIZE,
        SAL_PRIO_DHT,
        NULL
    );
}