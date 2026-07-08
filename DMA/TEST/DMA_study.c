#include <string.h>
#include <stdio.h>
#include "main.h"
#include "adc.h"
#include "dma.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"
#include "DMA_study.h"

#define ADC_BUF_LEN 100

static uint16_t buf0[ADC_BUF_LEN] __attribute__((aligned(4))); // M0AR 第一缓存
static uint16_t buf1[ADC_BUF_LEN] __attribute__((aligned(4))); // M1AR 第二缓存

#define ADC_BUF_TOTAL   (2 * ADC_BUF_LEN)
// 单块连续总缓存
static uint16_t adc_soft_total_buf[ADC_BUF_TOTAL] __attribute__((aligned(4)));
#define soft_buf0 (&adc_soft_total_buf[0])
#define soft_buf1 (&adc_soft_total_buf[ADC_BUF_LEN])

static  uint16_t adc_dma_buf[ADC_BUF_LEN];  // DMA存储ADC值
static SemaphoreHandle_t adcFullSem;  // DMA缓冲区满信号量
static  SemaphoreHandle_t lcdMutex;

static SemaphoreHandle_t semBuf0, semBuf1;

extern DMA_HandleTypeDef hdma_adc1;


static void Buf0_Complete(DMA_HandleTypeDef *hdma);
static void Buf1_Complete(DMA_HandleTypeDef *hdma);
static void My_DMA_ErrorCallback(DMA_HandleTypeDef *hdma);

static void DMA_Error_Callback(DMA_HandleTypeDef *hdma);
static void DMA_HalfComplete_Callback(DMA_HandleTypeDef *hdma);
static void DMA_TransComplete_Callback(DMA_HandleTypeDef *hdma);

static  void lcd_safe_print(uint16_t x, uint16_t y, char *str)
{
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    lcd_show_string(x, y, 240, 24, 24, str, RED);
    xSemaphoreGive(lcdMutex);
}
void test_init(void)
{
	lcd_init();
	lcdMutex = xSemaphoreCreateMutex();
	adcFullSem = xSemaphoreCreateBinary();  // 创建二值信号量
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf, ADC_BUF_LEN);	//启动ADC 规则通道 DMA 循环采样
}
void test_Double_Buffer_init(void)
{
	HAL_StatusTypeDef status;
	lcd_init();
	lcdMutex = xSemaphoreCreateMutex();
	semBuf0 = xSemaphoreCreateBinary();
	semBuf1 = xSemaphoreCreateBinary();
	if(semBuf0==NULL||semBuf1==NULL)
	{
		lcd_safe_print(0,62,"Binary_ERROR");
	}
	
	// 1.注册DMA双缓冲回调指针（核心，中断才能进业务代码）
	HAL_DMA_RegisterCallback(&hdma_adc1, HAL_DMA_XFER_CPLT_CB_ID, Buf0_Complete);
	HAL_DMA_RegisterCallback(&hdma_adc1, HAL_DMA_XFER_M1CPLT_CB_ID, Buf1_Complete);
	HAL_DMA_RegisterCallback(&hdma_adc1, HAL_DMA_XFER_ERROR_CB_ID, My_DMA_ErrorCallback);

	// 2. 硬件双缓冲DMA启动
	status =HAL_DMAEx_MultiBufferStart_IT(
		&hdma_adc1,
		(uint32_t)&ADC1->DR,  // Src：外设ADC DR源地址
		(uint32_t)buf0,                  // Dst：M0AR 第一块缓存
		(uint32_t)buf1,                  // SecondMem：M1AR 第二块缓存
		ADC_BUF_LEN                      // 单次传输长度
	);
	
	// 3. 手动使能ADC的DMA请求（CR2中的DMA位和DDS位）
	hadc1.Instance->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS;
	HAL_ADC_Start(&hadc1); // 内部自动置ADON、触发SWSTART
	//HAL_ADC_Start(&hadc1);
	if(HAL_OK!=status)
	{
		lcd_safe_print(0,48,"ERROR");
	}
}

void test_Double_Buffer_soft_init(void)
{
	HAL_StatusTypeDef status;
	lcd_init();
    lcdMutex = xSemaphoreCreateMutex();
    semBuf0 = xSemaphoreCreateBinary();
    semBuf1 = xSemaphoreCreateBinary();
	
    if(semBuf0==NULL||semBuf1==NULL||lcdMutex==NULL)
    {
        lcd_safe_print(0,62,"Sem_ERR");
        return;
    }
	
    // 注册DMA中断回调：半传输HT、传输完成TC、错误回调
    HAL_DMA_RegisterCallback(&hdma_adc1, HAL_DMA_XFER_HALFCPLT_CB_ID, DMA_HalfComplete_Callback);
    HAL_DMA_RegisterCallback(&hdma_adc1, HAL_DMA_XFER_CPLT_CB_ID, DMA_TransComplete_Callback);
    HAL_DMA_RegisterCallback(&hdma_adc1, HAL_DMA_XFER_ERROR_CB_ID, DMA_Error_Callback);

    // 启动循环DMA
    status = HAL_DMA_Start_IT(
        &hdma_adc1,
        (uint32_t)&ADC1->DR,
        (uint32_t)adc_soft_total_buf,
        ADC_BUF_TOTAL
    );

    // 开启ADC DMA持续请求
    hadc1.Instance->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS;
    // 最后启动ADC转换
    HAL_ADC_Start(&hadc1);
	// 清除ADC溢出锁死标志
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
    if(status != HAL_OK)
    {
		lcd_safe_print(0,48,"DMA_START_ERR");
    }
}

void test_normal(void* prame)
{
	char log_buf[64];
	while(1)
	{
		if(xSemaphoreTake(adcFullSem, pdMS_TO_TICKS(500)) == pdPASS)
		{
			snprintf(log_buf, sizeof(log_buf), "Norm ADC:%d    ", adc_dma_buf[0]);
			lcd_safe_print(0,0,log_buf);
			// Normal模式必须重新启动DMA
			HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf, ADC_BUF_LEN);
			
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

void test_Circular(void* prame)
{
	char log_buf[64];
	uint16_t latest_adc;
	while(1)
	{
		// 等待DMA缓冲区写满一次
		if(xSemaphoreTake(adcFullSem, pdMS_TO_TICKS(500)) == pdPASS)
		{
			// Circular循环，最后写入的有效数据在数组末尾 adc_dma_buf[ADC_BUF_LEN-1]
			latest_adc = adc_dma_buf[ADC_BUF_LEN - 1];
			snprintf(log_buf, sizeof(log_buf), "Cir ADC:%d    ", latest_adc);
			lcd_safe_print(0, 0, log_buf);
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}


void test_Double_Buffer(void* prame)
{
	char log_buf[64];
	uint32_t sum;
	uint16_t avg;

	while(1)
	{
		// 处理buf0
		if(xSemaphoreTake(semBuf0, pdMS_TO_TICKS(10)) == pdPASS)
		{
			sum = 0;
			for(int i=0;i<ADC_BUF_LEN;i++) sum += buf0[i];
			avg = sum / ADC_BUF_LEN;
			snprintf(log_buf, sizeof(log_buf), "HW DBuf0: %d  ", avg);
			lcd_safe_print(0, 0, log_buf);
		}
		// 处理buf1
		if(xSemaphoreTake(semBuf1, pdMS_TO_TICKS(10)) == pdPASS)
		{
			sum = 0;
			for(int i=0;i<ADC_BUF_LEN;i++) sum += buf1[i];
			avg = sum / ADC_BUF_LEN;
			snprintf(log_buf, sizeof(log_buf), "HW DBuf1: %d  ", avg);
			lcd_safe_print(0, 30, log_buf);
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

void test_Double_Buffer_soft(void* prame)
{
	char log_buf[64];
	uint32_t sum;
	uint16_t avg;
	uint16_t local_tmp[ADC_BUF_LEN]; // 私有拷贝缓存，隔离DMA
	while(1)
	{
		// 处理buf0
		if(xSemaphoreTake(semBuf0, pdMS_TO_TICKS(10)) == pdPASS)
		{
			// 第一步快速拷贝，释放DMA原始内存,防止cpu处理速度跟不上 DMA 时，读写内存冲突
			memcpy(local_tmp, soft_buf0, sizeof(local_tmp));
			sum = 0;
			for(int i=0;i<ADC_BUF_LEN;i++) sum += local_tmp[i];
			avg = sum / ADC_BUF_LEN;
			snprintf(log_buf, sizeof(log_buf), "softBuf0: %d  ", avg);
			lcd_safe_print(0, 0, log_buf);
		}
		// 处理buf1
		if(xSemaphoreTake(semBuf1, pdMS_TO_TICKS(10)) == pdPASS)
		{
			memcpy(local_tmp, soft_buf1, sizeof(local_tmp));
			sum = 0;
			for(int i=0;i<ADC_BUF_LEN;i++) sum +=  local_tmp[i];
			avg = sum / ADC_BUF_LEN;
			snprintf(log_buf, sizeof(log_buf), "softBuf1: %d  ", avg);
			lcd_safe_print(0, 30, log_buf);
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}

}
	

// DMA硬件双缓冲M0/M1完成回调
static void Buf0_Complete(DMA_HandleTypeDef *hdma)
{
    BaseType_t xWake = pdFALSE;
    xSemaphoreGiveFromISR(semBuf0, &xWake);
    portYIELD_FROM_ISR(xWake);
}
static void Buf1_Complete(DMA_HandleTypeDef *hdma)
{
    BaseType_t xWake = pdFALSE;
    xSemaphoreGiveFromISR(semBuf1, &xWake);
    portYIELD_FROM_ISR(xWake);
}
void My_DMA_ErrorCallback(DMA_HandleTypeDef *hdma)
{
	 if(hdma->Instance == DMA2_Stream2)
	 {
	   // 清除错误，重启双缓冲区DMA
	   HAL_DMA_Abort(&hdma_adc1);
		HAL_DMAEx_MultiBufferStart_IT(
			&hdma_adc1,
			(uint32_t)&hadc1.Instance->DR,  // Src：外设ADC DR源地址
			(uint32_t)buf0,                  // Dst：M0AR 第一块缓存
			(uint32_t)buf1,                  // SecondMem：M1AR 第二块缓存
			ADC_BUF_LEN                      // 单次传输长度
		);
	 }
}





// 半传输中断：前半段buf0填满，DMA正在写入后半段buf1，CPU处理buf0
static void DMA_HalfComplete_Callback(DMA_HandleTypeDef *hdma)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if(hdma->Instance == DMA2_Stream0)
    {
        xSemaphoreGiveFromISR(semBuf0, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// 传输完成中断：整块缓存填满，DMA从头写入buf0，CPU处理后半段buf1
static void DMA_TransComplete_Callback(DMA_HandleTypeDef *hdma)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if(hdma->Instance == DMA2_Stream0)
    {
        xSemaphoreGiveFromISR(semBuf1, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// DMA错误恢复回调
static void DMA_Error_Callback(DMA_HandleTypeDef *hdma)
{
    if(hdma->Instance == DMA2_Stream0)
    {
        HAL_DMA_Abort(hdma);
        __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
        // 重启循环DMA
           // 启动循环DMA
		HAL_DMA_Start_IT(
        &hdma_adc1,
        (uint32_t)&ADC1->DR,
        (uint32_t)adc_soft_total_buf,
        ADC_BUF_TOTAL
		);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	if(hadc->Instance == ADC1)
	{
		// 释放信号量，通知任务缓冲区一整批数据就绪
		xSemaphoreGiveFromISR(adcFullSem, NULL);
	}
}
