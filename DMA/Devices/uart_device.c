#include "stm32f4xx_hal.h"
#include "usart.h"
#include "uart_device.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include <string.h>

#define UART_RX_QUEUE_LEN 100
#define UART_MAX_DEVICE_NUM 12   // 可根据芯片串口数量放大

static struct UART_Device *uart_dev_map[UART_MAX_DEVICE_NUM] = {NULL};
static BaseType_t uart_dev_map_cnt = 0;

// 前置声明
static int stm32_uart_init(struct UART_Device *pDev, int baud, int datas, char parity, int stop);
static int stm32_uart_send(struct UART_Device *pDev, uint8_t *datas, int len, int timeout_ms);
static int stm32_uart_recv(struct UART_Device *pDev, uint8_t *data, int timeout_ms);

static int stm32_bear_uart_init(struct UART_Device *pDev, int baud, int datas, char parity, int stop);
static int stm32_bear_uart_send(struct UART_Device *pDev, uint8_t *datas, int len, int timeout_ms);
static int stm32_bear_uart_recv(struct UART_Device *pDev, uint8_t *data, int timeout_ms);

static int stm32_dma_uart_init(struct UART_Device *pDev, int baud, int datas, char parity, int stop);
static int stm32_dma_uart_send(struct UART_Device *pDev, uint8_t *datas, int len, int timeout_ms);
static int stm32_dma_uart_recv(struct UART_Device *pDev, uint8_t *data, int timeout_ms);

// 内部结构体，不透明
struct UART_Device {
    char *name;
    int (*Init)(struct UART_Device *pDev, int baud, int datas, char parity, int stop);
    int (*Send)(struct UART_Device *pDev, uint8_t *datas, int len, int timeout_ms);
    int (*Recv)(struct UART_Device *pDev, uint8_t *data, int timeout_ms);
    void *priv_data;
	UART_HandleTypeDef *huart;  // 新增：当前设备绑定的串口句柄
};

typedef struct {
    UART_HandleTypeDef *handle;
    SemaphoreHandle_t xTxSem;
    QueueHandle_t xRxQueue;
    uint8_t rxdata[100];
	int rx_offset;  //记录DMA读到哪里了
} UART_Data;

// ======================= 绑定串口句柄 =======================
// 中断模式私有数据
static UART_Data g_uart1_it_data = {
    .handle = &huart1,
};

// 轮询阻塞模式私有数据
static UART_Data g_uart1_poll_data = {
    .handle = &huart1,
};

// DMA空闲中断模式私有数据
static UART_Data g_uart1_dma_data = {
    .handle = &huart1,
};

static struct UART_Device g_stm32_uart1 = {
    .name = "stm32_uart1",
    .Init = stm32_uart_init,
    .Send = stm32_uart_send,
    .Recv = stm32_uart_recv,
    .priv_data = &g_uart1_it_data,
};
static struct UART_Device g_stm32_bear_uart1 = {
    .name = "stm32_bear_uart1",
    .Init = stm32_bear_uart_init,
    .Send = stm32_bear_uart_send,
    .Recv = stm32_bear_uart_recv,
    .priv_data = &g_uart1_poll_data,
};
static struct UART_Device g_stm32_dma_uart1 = {
    "stm32_dma_uart1",
    stm32_dma_uart_init,
    stm32_dma_uart_send,
    stm32_dma_uart_recv,
    &g_uart1_dma_data,
};
static struct UART_Device *g_uart_devs[] = {&g_stm32_uart1,&g_stm32_bear_uart1,&g_stm32_dma_uart1};

// ======================= 对外API =======================
UART_Device *UART_GetDevice(const char *name)
{
    for (int i = 0; i < sizeof(g_uart_devs)/sizeof(g_uart_devs[0]); i++) 
	{
        if (strcmp(name, g_uart_devs[i]->name) == 0)
            return (UART_Device*)g_uart_devs[i];
    }
    return NULL;
}

int UART_Init(UART_Device *pDev, int baud, int datas, char parity, int stop)
{
	if (pDev == NULL) return -1;  // 空指针判断
    return pDev->Init(pDev, baud, datas, parity, stop);
}

int UART_Send(UART_Device *pDev, uint8_t *datas, int len, int timeout_ms)
{
    return pDev->Send(pDev, datas, len, timeout_ms);
}

int UART_Recv(UART_Device *pDev, uint8_t *data, int timeout_ms)
{
    return pDev->Recv(pDev, data, timeout_ms);
}

// ======================= 底层实现 =======================
// 注册设备到映射表，Init时调用
static BaseType_t UART_Device_Register(struct UART_Device *dev)
{
	// 1. 判断表格是否存满
    if(uart_dev_map_cnt >= UART_MAX_DEVICE_NUM)
        return pdFALSE;
     // 2. 遍历表格，去重：同一个设备不要重复存入数组
    for(int i = 0; i < uart_dev_map_cnt; i++)
    {
        if(uart_dev_map[i] == dev)
            return pdTRUE;
    }
	// 3. 表格有空位、且未重复，存入设备指针，计数+1
    uart_dev_map[uart_dev_map_cnt++] = dev;
    return pdTRUE;
}
// 核心：根据huart句柄查找对应设备
static struct UART_Device *UART_Device_FindByHandle(UART_HandleTypeDef *huart)
{
	// 遍历所有已经注册的设备
    for(int i = 0; i < uart_dev_map_cnt; i++)
    {
        struct UART_Device *dev = uart_dev_map[i];
		// 判空 + 对比设备绑定的huart和中断触发的huart
        if(dev && dev->huart == huart)
        {
            return dev;// 匹配到，返回该串口设备指针
        }
    }
    return NULL;// 表里没有对应串口设备
}
// ======================= 中断 =======================
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t xWoken = pdFALSE;
    struct UART_Device *dev = UART_Device_FindByHandle(huart);
    if(dev == NULL) return;

    UART_Data *data = dev->priv_data;
    xSemaphoreGiveFromISR(data->xTxSem, &xWoken);
    portYIELD_FROM_ISR(xWoken);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t xWoken = pdFALSE;
    struct UART_Device *dev = UART_Device_FindByHandle(huart);
    if(dev == NULL) return;

    UART_Data *data = dev->priv_data;
    int len = huart->RxXferSize - huart->RxXferCount;

    for(int i = 0; i < len; i++)
    {
        xQueueSendFromISR(data->xRxQueue, &data->rxdata[i], &xWoken);
    }
    // 仅IT模式重新开启单字节接收
    if(dev->Init == stm32_uart_init)
    {
        HAL_UART_Receive_IT(data->handle, data->rxdata, 1);
    }
    portYIELD_FROM_ISR(xWoken);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    BaseType_t xWoken = pdFALSE;
    struct UART_Device *dev = UART_Device_FindByHandle(huart);
    if(dev == NULL) return;

    UART_Data *data = dev->priv_data;
    int start = data->rx_offset;
    for(int i = start; i < Size; i++)
    {
        xQueueSendFromISR(data->xRxQueue, &data->rxdata[i], &xWoken);
    }
    data->rx_offset = Size;

    if(huart->RxEventType == HAL_UART_RXEVENT_TC)
    {
        data->rx_offset = 0;
    }
    portYIELD_FROM_ISR(xWoken);
}

// ======================= FreeRTOS 中断方式实现 =======================
static int stm32_uart_init(struct UART_Device *pDev, int baud, int datas, char parity, int stop)
{
    UART_Data *data = (UART_Data*)pDev->priv_data;
	if(UART_Device_Register(pDev) != pdTRUE)
		return -4; // 设备注册失败
    data->xTxSem = xSemaphoreCreateBinary();//作用：让我知道发送是否完成
    data->xRxQueue = xQueueCreate(UART_RX_QUEUE_LEN, 1);
	if(data->xTxSem == NULL || data->xRxQueue == NULL)
		return -3;
	xSemaphoreGive(data->xTxSem);//让第一次可以发送
	 data->rx_offset = 0;
    /* 启动第1次数据的接收 */
    HAL_UART_Receive_IT(data->handle, (uint8_t*)&data->rxdata, 1);

    return 0;
}

static int stm32_uart_send(struct UART_Device *pDev, uint8_t *datas, int len, int timeout_ms)
{
    UART_Data *data = (UART_Data*)pDev->priv_data;
    if (xSemaphoreTake(data->xTxSem, pdMS_TO_TICKS(timeout_ms)) != pdPASS)
        return -1;

    if (HAL_UART_Transmit_IT(data->handle, datas, len) != HAL_OK) 
	{
        xSemaphoreGive(data->xTxSem);
        return -1;
    }
    return 0;
}

static int stm32_uart_recv(struct UART_Device *pDev, uint8_t *data, int timeout_ms)
{
    UART_Data *uart_data = (UART_Data*)pDev->priv_data;
     /* 读取队列得到数据, 问题:谁写队列?中断:写队列 */
    if (pdPASS == xQueueReceive(uart_data->xRxQueue, data,pdMS_TO_TICKS(timeout_ms)))
        return 0;
    else
        return -1;
}

// ======================= 裸机查询方式= ================================

static int stm32_bear_uart_init(struct UART_Device *pDev, int baud, int datas, char parity, int stop)
{
    UART_Device_Register(pDev);
    return 0;
}

static int stm32_bear_uart_send(struct UART_Device *pDev, uint8_t *datas, int len, int timeout_ms)
{
    UART_Data *priv = (UART_Data*)pDev->priv_data;
    if (HAL_UART_Transmit(priv->handle, datas, len, timeout_ms) == HAL_OK) {
        return 0;
    } else {
        return -1;
    }
}

static int stm32_bear_uart_recv(struct UART_Device *pDev, uint8_t *data, int timeout_ms)
{
    UART_Data *priv = (UART_Data*)pDev->priv_data;
    if (HAL_UART_Receive(priv->handle, data, 1, timeout_ms) == HAL_OK) {
        return 0;
    } else {
        return -1;
    }
}

//======================= DMA+FreeRTOS(队列,信号量)=======================
static int stm32_dma_uart_init(struct UART_Device *pDev, int baud, int datas, char parity, int stop)
{
    UART_Data *data = pDev->priv_data;

    if(UART_Device_Register(pDev) != pdTRUE)
        return -4;

    data->xTxSem = xSemaphoreCreateBinary();
    data->xRxQueue = xQueueCreate(UART_RX_QUEUE_LEN, 1);
	
    if(data->xTxSem == NULL || data->xRxQueue == NULL)
        return -3;
    data->rx_offset = 0;
    /* 启动第1次数据的接收 */
    //HAL_UART_Receive_DMA(data->handle, &data->rxdatas, 100);
    HAL_UARTEx_ReceiveToIdle_DMA(data->handle, data->rxdata, 10);
    
    return 0;
}
static int stm32_dma_uart_send(struct UART_Device *pDev, uint8_t *datas, int len, int timeout_ms)
{
    UART_Data *data = pDev->priv_data;
    
    /* 仅仅是触发中断而已 */
    HAL_UART_Transmit_DMA(data->handle, datas, len);

    /* 等待发送完毕:等待信号量 */
    if (pdTRUE == xSemaphoreTake(data->xTxSem, timeout_ms))
        return 0;
    else
        return -1;
}

static int stm32_dma_uart_recv(struct UART_Device *pDev, uint8_t *data, int timeout_ms)
{
    UART_Data *uart_data = pDev->priv_data;
    
    /* 读取队列得到数据, 问题:谁写队列?中断:写队列 */
    if (pdPASS == xQueueReceive(uart_data->xRxQueue, data,timeout_ms))
        return 0;
    else
        return -1;
}
