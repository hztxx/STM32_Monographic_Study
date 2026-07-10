/**
 * @file perf_test2.c
 * @brief 性能分析工具使用示例--算法测试
 */
#include <string.h>
#include <stdio.h>
#include "main.h"

#include "perf_counter.h"
#include "perf_macros.h"
#include "perf_test2.h"


/**
 * @brief 冒泡排序
 */
void bubble_sort(int *arr, int n) {
    PERF_FUNCTION_START();
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }

    PERF_FUNCTION_STOP();
}

/**
 * @brief 快速排序
 */
void quick_sort(int *arr, int low, int high) {
    if (low < high) {
        int pivot = arr[high];
        int i = low - 1;

        for (int j = low; j < high; j++) {
            if (arr[j] < pivot) {
                i++;
                int temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }

        int temp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = temp;

        int pi = i + 1;
        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

void quick_sort_wrapper(int *arr, int n) {
    PERF_Start("quick_sort");
    quick_sort(arr, 0, n - 1);
    PERF_Stop("quick_sort");
}

/**
 * @brief 排序算法性能对比
 */
void test_sorting_algorithms(void) {
   
     const int SIZE = 10;
	static int arr1[SIZE];
	static int arr2[SIZE];
    // 初始化数组
    for (int i = 0; i < SIZE; i++) {
        arr1[i] = SIZE - i;  // 逆序
        arr2[i] = SIZE - i;
    }

    printf("\n排序算法性能对比 (数组大小: %d)\n", SIZE);
    printf("=====================================\n");

    // 测试冒泡排序
    for (int i = 0; i < 10; i++) {
        // 重新初始化数组
        for (int j = 0; j < SIZE; j++) {
            arr1[j] = SIZE - j;
        }
        bubble_sort(arr1, SIZE);
    }

    // 测试快速排序
    for (int i = 0; i < 10; i++) {
        // 重新初始化数组
        for (int j = 0; j < SIZE; j++) {
            arr2[j] = SIZE - j;
        }
        quick_sort_wrapper(arr2, SIZE);
    }

    PERF_Report();

    // 计算加速比
    uint32_t bubble_cycles, quick_cycles;
    if (PERF_GetCycles("bubble_sort", &bubble_cycles) == 0 &&
        PERF_GetCycles("quick_sort", &quick_cycles) == 0) {
        float speedup = (float)bubble_cycles / quick_cycles;
        printf("\n快速排序加速比: %.2fx\n", speedup);
    }
}

int test2(void)
{    
	// 初始化性能分析器（CPU频率为168MHz）
    if (PERF_Init(168) != 0) {
        printf("性能分析器初始化失败\n");
        return -1;
    }
	test_sorting_algorithms();
	while(1)
	{

	}
	return 0;
}