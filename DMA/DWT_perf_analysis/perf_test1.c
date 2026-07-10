/**
 * @file perf_test1.c
 * @brief 性能分析工具使用示例--基础用法
 */
#include <string.h>
#include <stdio.h>
#include "main.h"

#include "perf_counter.h"
#include "perf_macros.h"
#include "perf_test1.h"
/* 测试函数 */
void test_function_1(void) {
    PERF_FUNCTION_START();

    // 模拟一些计算
    volatile int sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i;
    }

    PERF_FUNCTION_STOP();
}

void test_function_2(void) {
    PERF_FUNCTION_START();

    // 模拟更复杂的计算
    volatile float result = 0.0f;
    for (int i = 0; i < 1000; i++) {
        result += (float)i * 1.5f;
    }

    PERF_FUNCTION_STOP();
}

void test_nested_measurement(void) {
    PERF_Start("outer_function");

    // 外层代码
    volatile int x = 0;
    for (int i = 0; i < 100; i++) {
        x += i;
    }

    // 内层测量
    PERF_Start("inner_function");
    volatile int y = 0;
    for (int i = 0; i < 200; i++) {
        y += i * 2;
    }
    PERF_Stop("inner_function");

    // 外层代码继续
    for (int i = 0; i < 100; i++) {
        x -= i;
    }

    PERF_Stop("outer_function");
}
int test1(void) {

    // 初始化性能分析器（假设CPU频率为168MHz）
    if (PERF_Init(168) != 0) {
        printf("性能分析器初始化失败\n");
        return -1;
    }

    printf("性能分析工具示例\n");
    printf("==================\n\n");

    // 示例1: 基本测量
    printf("示例1: 基本函数性能测量\n");
    for (int i = 0; i < 10; i++) {
        test_function_1();
        test_function_2();
    }
    PERF_Report();

    // 示例2: 嵌套测量
    printf("\n示例2: 嵌套性能测量\n");
    PERF_Reset(NULL);  // 复位所有计数器
    for (int i = 0; i < 5; i++) {
        test_nested_measurement();
    }
    PERF_Report();

    // 示例3: 详细报告
    printf("\n示例3: 详细性能报告\n");
    PERF_ReportDetailed("test_function_1");

    // 示例4: JSON输出
    printf("\n示例4: JSON格式输出\n");
    PERF_ReportJSON();

    while (1) 
	{
        // 主循环
    }

    return 0;
}
