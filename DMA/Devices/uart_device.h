#ifndef __UART_DEVICE_H
#define __UART_DEVICE_H

#include <stdint.h>
typedef struct UART_Device UART_Device;

UART_Device *UART_GetDevice(const char *name);
int UART_Init(UART_Device *pDev, int baud, int datas, char parity, int stop);
int UART_Send(UART_Device *pDev, uint8_t *datas, int len, int timeout_ms);
int UART_Recv(UART_Device *pDev, uint8_t *data, int timeout_ms);


#endif
