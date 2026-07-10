/**
 * @file main.c
 * @brief 性能分析工具使用示例--实时系统性能监控
 */
#include <string.h>
#include <stdio.h>
#include "main.h"

#include "perf_counter.h"
#include "perf_macros.h"
#include "perf_test3.h"

/**
 * @brief 实时任务性能监控
 */

/* 模拟RTOS任务 */
void task_sensor_read(void) {
    PERF_Start("task_sensor");

    // 模拟传感器读取
    volatile int sensor_data = 0;
    for (int i = 0; i < 500; i++) {
        sensor_data += i;
    }

    PERF_Stop("task_sensor");
}

void task_data_process(void) {
    PERF_Start("task_process");

    // 模拟数据处理
    volatile float result = 0.0f;
    for (int i = 0; i < 300; i++) {
        result += (float)i * 0.5f;
    }

    PERF_Stop("task_process");
}

void task_communication(void) {
    PERF_Start("task_comm");

    // 模拟通信
    volatile int checksum = 0;
    for (int i = 0; i < 200; i++) {
        checksum ^= i;
    }

    PERF_Stop("task_comm");
}

/**
 * @brief 系统性能监控循环
 */
void system_performance_monitor(void) {
    static uint32_t report_counter = 0;
    const uint32_t REPORT_INTERVAL = 1000;  // 每1000次循环报告一次

    while (1) {
        // 执行各个任务
        task_sensor_read();
        task_data_process();
        task_communication();

        report_counter++;

        // 定期生成报告
        if (report_counter >= REPORT_INTERVAL) {
            printf("\n=== 系统性能报告 (循环: %u) ===\n", 
                   (unsigned int)report_counter);
            PERF_Report();

            // 检查是否有任务超时
            uint32_t sensor_cycles, process_cycles, comm_cycles;
            PERF_GetCycles("task_sensor", &sensor_cycles);
            PERF_GetCycles("task_process", &process_cycles);
            PERF_GetCycles("task_comm", &comm_cycles);

            // 假设每个任务的超时阈值（周期数）
            const uint32_t SENSOR_TIMEOUT = 100000;
            const uint32_t PROCESS_TIMEOUT = 80000;
            const uint32_t COMM_TIMEOUT = 50000;

            if (sensor_cycles > SENSOR_TIMEOUT) {
                printf("[警告] 传感器任务超时: %u 周期\n", 
                       (unsigned int)sensor_cycles);
            }
            if (process_cycles > PROCESS_TIMEOUT) {
                printf("[警告] 处理任务超时: %u 周期\n", 
                       (unsigned int)process_cycles);
            }
            if (comm_cycles > COMM_TIMEOUT) {
                printf("[警告] 通信任务超时: %u 周期\n", 
                       (unsigned int)comm_cycles);
            }

            report_counter = 0;
            PERF_Reset(NULL);  // 复位计数器
        }

        // 模拟延时
        for (volatile int i = 0; i < 10000; i++);
    }
}

int test3(void)
{
    // 初始化性能分析器（假设CPU频率为168MHz）
    if (PERF_Init(168) != 0) {
        printf("性能分析器初始化失败\n");
        return -1;
    }
	system_performance_monitor();
}
