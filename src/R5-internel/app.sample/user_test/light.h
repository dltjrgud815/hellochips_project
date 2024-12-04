//light.h
#ifndef Light_H
#define Light_H
#include <sal_internal.h>
#include <i2c_reg.h>

#define LIGHT_STK_SIZE               (256UL)
#define I2C_TEST_CLK_RATE_100           (100)
#define I2C_TEST_CLK_RATE_400           (400)
//void externalFunction(void);
//static void ConsoleTask(void *pArg);
void LIGHT_Task(void);
//uint16 I2C_YL40_ReadData(uint8 channel);
#endif // Light_H
