#include <string.h>
#include <stdio.h>
#include "main.h"

#include "lcd.h"
#include "perf_counter.h"
#include "usart.h"

int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

/* 全局性能分析器实例 */
static perf_analyzer_t g_perf_analyzer = {0};

/**
 * @brief 初始化DWT
 * @param cpu_freq_mhz CPU频率（MHz）
 * @retval 0: 成功, -1: 失败
 */
int PERF_DWT_Init(uint32_t cpu_freq_mhz) {
    // 使能DWT和ITM
    CoreDebug_DEMCR |= CoreDebug_DEMCR_TRCENA;

    // 复位周期计数器
    DWT_CYCCNT = 0;

    // 使能周期计数器
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;

    // 使能其他计数器（可选）
    DWT_CTRL |= DWT_CTRL_CPIEVTENA;   // CPI计数
    DWT_CTRL |= DWT_CTRL_EXCEVTENA;   // 异常计数
    DWT_CTRL |= DWT_CTRL_SLEEPEVTENA; // 睡眠计数
    DWT_CTRL |= DWT_CTRL_LSUEVTENA;   // LSU计数
    DWT_CTRL |= DWT_CTRL_FOLDEVTENA;  // 折叠计数

    // 清零所有计数器
    DWT_CPICNT = 0;
    DWT_EXCCNT = 0;
    DWT_SLEEPCNT = 0;
    DWT_LSUCNT = 0;
    DWT_FOLDCNT = 0;

    // 验证DWT是否工作
    uint32_t test_start = DWT_CYCCNT;
    for (volatile int i = 0; i < 100; i++);
    uint32_t test_end = DWT_CYCCNT;

    if (test_end <= test_start) {
        return -1;  // DWT未工作
    }

    g_perf_analyzer.cpu_freq_mhz = cpu_freq_mhz;
    g_perf_analyzer.initialized = true;

    return 0;
}

/**
 * @brief 读取DWT快照
 * @param snapshot 快照结构指针
 */
static void PERF_DWT_ReadSnapshot(perf_snapshot_t *snapshot) {
    // 读取所有计数器（原子操作）
    uint32_t primask = __get_PRIMASK();//保存当前全局中断屏蔽状态
    __disable_irq();//关闭所有全局中断

    snapshot->cycles = DWT_CYCCNT;
    snapshot->cpi_count = DWT_CPICNT;
    snapshot->exc_count = DWT_EXCCNT;
    snapshot->sleep_count = DWT_SLEEPCNT;
    snapshot->lsu_count = DWT_LSUCNT;
    snapshot->fold_count = DWT_FOLDCNT;
    snapshot->timestamp = DWT_CYCCNT;  // 使用周期计数作为时间戳

    __set_PRIMASK(primask);//恢复进代码前的中断状态
}

/**
 * @brief 计算两个快照之间的差值
 * @param start 开始快照
 * @param end 结束快照
 * @param diff 差值快照
 */
static void PERF_DWT_CalcDiff(const perf_snapshot_t *start,
                               const perf_snapshot_t *end,
                               perf_snapshot_t *diff) {
    // 处理计数器溢出（32位）
    diff->cycles = (end->cycles >= start->cycles) ?
                   (end->cycles - start->cycles) :
                   (0xFFFFFFFF - start->cycles + end->cycles + 1);

    diff->cpi_count = (end->cpi_count >= start->cpi_count) ?
                      (end->cpi_count - start->cpi_count) :
                      (0xFFFFFFFF - start->cpi_count + end->cpi_count + 1);

    diff->exc_count = (end->exc_count >= start->exc_count) ?
                      (end->exc_count - start->exc_count) :
                      (0xFFFFFFFF - start->exc_count + end->exc_count + 1);

    diff->sleep_count = (end->sleep_count >= start->sleep_count) ?
                        (end->sleep_count - start->sleep_count) :
                        (0xFFFFFFFF - start->sleep_count + end->sleep_count + 1);

    diff->lsu_count = (end->lsu_count >= start->lsu_count) ?
                      (end->lsu_count - start->lsu_count) :
                      (0xFFFFFFFF - start->lsu_count + end->lsu_count + 1);

    diff->fold_count = (end->fold_count >= start->fold_count) ?
                       (end->fold_count - start->fold_count) :
                       (0xFFFFFFFF - start->fold_count + end->fold_count + 1);
}

/**
 * @brief 复位DWT计数器
 */
void PERF_DWT_Reset(void) {
    DWT_CYCCNT = 0;
    DWT_CPICNT = 0;
    DWT_EXCCNT = 0;
    DWT_SLEEPCNT = 0;
    DWT_LSUCNT = 0;
    DWT_FOLDCNT = 0;
}


/**
 * @brief 初始化性能分析器
 * @param cpu_freq_mhz CPU频率（MHz）
 * @retval 0: 成功, -1: 失败
 */
int PERF_Init(uint32_t cpu_freq_mhz) {
    if (g_perf_analyzer.initialized) {
        return 0;  // 已初始化
    }

    // 清零分析器
    memset(&g_perf_analyzer, 0, sizeof(perf_analyzer_t));

    // 初始化DWT
    if (PERF_DWT_Init(cpu_freq_mhz) != 0) {
        return -1;
    }

    printf("[PERF] 性能分析器已初始化 (CPU: %u MHz)\n", 
           (unsigned int)cpu_freq_mhz);

    return 0;
}

/**
 * @brief 查找或创建计数器
 * @param name 计数器名称
 * @retval 计数器指针，NULL表示失败
 */
static perf_counter_t* PERF_FindOrCreateCounter(const char *name) {
    // 1. 先遍历已有计数器，同名直接返回指针
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        if (strcmp(g_perf_analyzer.counters[i].name, name) == 0) {
            return &g_perf_analyzer.counters[i];
        }
    }

    // 2. 不存在则新建，先判断是否达到最大测点上限
    if (g_perf_analyzer.counter_count >= PERF_MAX_COUNTERS) {
        printf("[PERF] 错误: 计数器数量已达上限\n");
        return NULL;
    }
	// 分配数组内下一个空位
    perf_counter_t *counter = &g_perf_analyzer.counters[g_perf_analyzer.counter_count++];

    // 3. 初始化新计数器默认参数
    memset(counter, 0, sizeof(perf_counter_t));
    strncpy(counter->name, name, PERF_NAME_MAX_LEN - 1);
    counter->name[PERF_NAME_MAX_LEN - 1] = '\0';// 强制字符串截断防溢出
    counter->state = PERF_STATE_IDLE;
    counter->enabled = true;
    counter->min_cycles = 0xFFFFFFFF;// 最小值初始设极大值，后续自动覆盖
    counter->max_cycles = 0; // 最大值初始0，后续自动更新
	
    return counter;
}

/**
 * @brief 开始性能测量
 * @param name 测量点名称
 * @retval 0: 成功, -1: 失败
 */
int PERF_Start(const char *name) {
    if (!g_perf_analyzer.initialized) {
        return -1;
    }
	// 自动获取/创建测点
    perf_counter_t *counter = PERF_FindOrCreateCounter(name);
    if (counter == NULL) {
        return -1;
    }
	// 测点被禁用，直接跳过测量
    if (!counter->enabled) {
        return 0;  // 计数器已禁用
    }
	// 状态机校验：不能连续两次Start
    if (counter->state == PERF_STATE_RUNNING) {
        printf("[PERF] 警告: 计数器 '%s' 已在运行\n", name);
        return -1;
    }

    // 抓取起点硬件快照
    PERF_DWT_ReadSnapshot(&counter->start);
    counter->state = PERF_STATE_RUNNING;

    return 0;
}

/**
 * @brief 停止性能测量
 * @param name 测量点名称
 * @retval 0: 成功, -1: 失败
 */
int PERF_Stop(const char *name) {
    if (!g_perf_analyzer.initialized) {
        return -1;
    }

    perf_counter_t *counter = PERF_FindOrCreateCounter(name);
    if (counter == NULL) {
        return -1;
    }

    if (!counter->enabled) {
        return 0;
    }
	// 状态校验：必须处于RUNNING才能停止
    if (counter->state != PERF_STATE_RUNNING) {
        printf("[PERF] 警告: 计数器 '%s' 未运行\n", name);
        return -1;
    }

     // 采集结束快照
    PERF_DWT_ReadSnapshot(&counter->end);
    counter->state = PERF_STATE_STOPPED;

    // 计算起止差值（自动处理32位溢出）
    perf_snapshot_t diff;
    PERF_DWT_CalcDiff(&counter->start, &counter->end, &diff);

    // 更新统计信息
    counter->call_count++;
    counter->total_cycles += diff.cycles;

    if (diff.cycles < counter->min_cycles) {
        counter->min_cycles = diff.cycles;
    }

    if (diff.cycles > counter->max_cycles) {
        counter->max_cycles = diff.cycles;
    }

    // 环形历史记录更新
    counter->history[counter->history_index] = diff.cycles;
    counter->history_index = (counter->history_index + 1) % PERF_HISTORY_SIZE;

    return 0;
}

/**
 * @brief 获取计数器统计信息
 * @param name 计数器名称
 * @param cycles 输出：周期数
 * @retval 0: 成功, -1: 失败
 */
int PERF_GetCycles(const char *name, uint32_t *cycles) {
    if (!g_perf_analyzer.initialized || cycles == NULL) {
        return -1;
    }
	// 遍历查找对应测点
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        if (strcmp(g_perf_analyzer.counters[i].name, name) == 0) {
            perf_counter_t *counter = &g_perf_analyzer.counters[i];
			// 计算平均值，无调用则返回0
            if (counter->call_count > 0) {
                *cycles = (uint32_t)(counter->total_cycles / counter->call_count);
            } else {
                *cycles = 0;
            }

            return 0;
        }
    }

    return -1;  // 未找到计数器
}

/**
 * @brief 复位计数器
 * @param name 计数器名称（NULL表示复位所有）
 */
void PERF_Reset(const char *name) {
    if (name == NULL) {
        // 复位所有计数器
        for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
            perf_counter_t *counter = &g_perf_analyzer.counters[i];
            counter->call_count = 0;
            counter->total_cycles = 0;
            counter->min_cycles = 0xFFFFFFFF;
            counter->max_cycles = 0;
            counter->history_index = 0;
            memset(counter->history, 0, sizeof(counter->history));
        }
    } else {
        // 复位指定计数器
        for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
            if (strcmp(g_perf_analyzer.counters[i].name, name) == 0) {
                perf_counter_t *counter = &g_perf_analyzer.counters[i];
                counter->call_count = 0;
                counter->total_cycles = 0;
                counter->min_cycles = 0xFFFFFFFF;
                counter->max_cycles = 0;
                counter->history_index = 0;
                memset(counter->history, 0, sizeof(counter->history));
                break;
            }
        }
    }
}

/**
 * @brief 启用/禁用计数器
 * @param name 计数器名称
 * @param enabled true: 启用, false: 禁用
 */
void PERF_SetEnabled(const char *name, bool enabled) {
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        if (strcmp(g_perf_analyzer.counters[i].name, name) == 0) {
            g_perf_analyzer.counters[i].enabled = enabled;
            break;
        }
    }
}
/**
 * @brief 打印性能报告
 */
void PERF_Report(void) {
    if (!g_perf_analyzer.initialized) {
        printf("[PERF] 错误: 性能分析器未初始化\n");
        return;
    }

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║              性能分析报告                                      ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║ CPU频率: %u MHz                                               ║\n", 
           (unsigned int)g_perf_analyzer.cpu_freq_mhz);
    printf("║ 计数器数量: %u                                                ║\n", 
           g_perf_analyzer.counter_count);
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    if (g_perf_analyzer.counter_count == 0) {
        printf("没有性能数据\n");
        return;
    }

    // 表头
    printf("┌────────────────────────┬────────┬──────────┬──────────┬──────────┬──────────┐\n");
    printf("│ 名称                   │ 调用次 │ 平均周期 │ 最小周期 │ 最大周期 │ 平均时间 │\n");
    printf("├────────────────────────┼────────┼──────────┼──────────┼──────────┼──────────┤\n");

    // 数据行
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        perf_counter_t *counter = &g_perf_analyzer.counters[i];

        if (counter->call_count == 0) {
            continue;
        }

        uint32_t avg_cycles = (uint32_t)(counter->total_cycles / counter->call_count);
        float avg_time_us = (float)avg_cycles / g_perf_analyzer.cpu_freq_mhz;

        printf("│ %-22s │ %6u │ %8u │ %8u │ %8u │ %7.2f  │\n",
               counter->name,
               (unsigned int)counter->call_count,
               (unsigned int)avg_cycles,
               (unsigned int)counter->min_cycles,
               (unsigned int)counter->max_cycles,
               avg_time_us);
    }

    printf("└────────────────────────┴────────┴──────────┴──────────┴──────────┴──────────┘\n");
    printf("\n");
}

/**
 * @brief 打印详细性能报告
 * @param name 计数器名称
 */
void PERF_ReportDetailed(const char *name) {
    if (!g_perf_analyzer.initialized) {
        return;
    }

    perf_counter_t *counter = NULL;
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        if (strcmp(g_perf_analyzer.counters[i].name, name) == 0) {
            counter = &g_perf_analyzer.counters[i];
            break;
        }
    }

    if (counter == NULL || counter->call_count == 0) {
        printf("[PERF] 未找到计数器 '%s' 或无数据\n", name);
        return;
    }

    uint32_t avg_cycles = (uint32_t)(counter->total_cycles / counter->call_count);
    float avg_time_us = (float)avg_cycles / g_perf_analyzer.cpu_freq_mhz;
    float min_time_us = (float)counter->min_cycles / g_perf_analyzer.cpu_freq_mhz;
    float max_time_us = (float)counter->max_cycles / g_perf_analyzer.cpu_freq_mhz;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  详细性能报告: %s\n", counter->name);
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  调用次数:     %u\n", (unsigned int)counter->call_count);
    printf("  总周期数:     %llu\n", (unsigned long long)counter->total_cycles);
    printf("  平均周期:     %u (%.2f μs)\n", (unsigned int)avg_cycles, avg_time_us);
    printf("  最小周期:     %u (%.2f μs)\n", (unsigned int)counter->min_cycles, min_time_us);
    printf("  最大周期:     %u (%.2f μs)\n", (unsigned int)counter->max_cycles, max_time_us);
    printf("  周期变化:     %u (%.1f%%)\n", 
           (unsigned int)(counter->max_cycles - counter->min_cycles),
           100.0f * (counter->max_cycles - counter->min_cycles) / avg_cycles);

    // 打印历史记录
    printf("\n  最近%d次测量:\n", PERF_HISTORY_SIZE);
    printf("  ┌");
    for (int i = 0; i < PERF_HISTORY_SIZE; i++) {
        printf("────────┬");
    }
    printf("\b┐\n");

    printf("  │");
    for (int i = 0; i < PERF_HISTORY_SIZE; i++) {
        int idx = (counter->history_index + i) % PERF_HISTORY_SIZE;
        if (counter->history[idx] > 0) {
            printf(" %6u │", (unsigned int)counter->history[idx]);
        } else {
            printf("   --   │");
        }
    }
    printf("\n");

    printf("  └");
    for (int i = 0; i < PERF_HISTORY_SIZE; i++) {
        printf("────────┴");
    }
    printf("\b┘\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");
}
/**
 * @brief 生成JSON格式报告
 */
void PERF_ReportJSON(void) {
    if (!g_perf_analyzer.initialized) {
        return;
    }

    printf("{\n");
    printf("  \"performance_report\": {\n");
    printf("    \"cpu_freq_mhz\": %u,\n", (unsigned int)g_perf_analyzer.cpu_freq_mhz);
    printf("    \"counter_count\": %u,\n", g_perf_analyzer.counter_count);
    printf("    \"counters\": [\n");

    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        perf_counter_t *counter = &g_perf_analyzer.counters[i];

        if (counter->call_count == 0) {
            continue;
        }

        uint32_t avg_cycles = (uint32_t)(counter->total_cycles / counter->call_count);
        float avg_time_us = (float)avg_cycles / g_perf_analyzer.cpu_freq_mhz;

        printf("      {\n");
        printf("        \"name\": \"%s\",\n", counter->name);
        printf("        \"call_count\": %u,\n", (unsigned int)counter->call_count);
        printf("        \"total_cycles\": %llu,\n", (unsigned long long)counter->total_cycles);
        printf("        \"avg_cycles\": %u,\n", (unsigned int)avg_cycles);
        printf("        \"min_cycles\": %u,\n", (unsigned int)counter->min_cycles);
        printf("        \"max_cycles\": %u,\n", (unsigned int)counter->max_cycles);
        printf("        \"avg_time_us\": %.2f,\n", avg_time_us);
        printf("        \"history\": [");

        for (int j = 0; j < PERF_HISTORY_SIZE; j++) {
            if (j > 0) printf(", ");
            printf("%u", (unsigned int)counter->history[j]);
        }

        printf("]\n");
        printf("      }");

        if (i < g_perf_analyzer.counter_count - 1) {
            printf(",");
        }
        printf("\n");
    }

    printf("    ]\n");
    printf("  }\n");
    printf("}\n");
}

/**
 * @brief 生成CSV格式报告
 */
void PERF_ReportCSV(void) {
    if (!g_perf_analyzer.initialized) {
        return;
    }

    // CSV表头
    printf("Name,CallCount,TotalCycles,AvgCycles,MinCycles,MaxCycles,AvgTimeUs\n");

    // 数据行
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        perf_counter_t *counter = &g_perf_analyzer.counters[i];

        if (counter->call_count == 0) {
            continue;
        }

        uint32_t avg_cycles = (uint32_t)(counter->total_cycles / counter->call_count);
        float avg_time_us = (float)avg_cycles / g_perf_analyzer.cpu_freq_mhz;

        printf("%s,%u,%llu,%u,%u,%u,%.2f\n",
               counter->name,
               (unsigned int)counter->call_count,
               (unsigned long long)counter->total_cycles,
               (unsigned int)avg_cycles,
               (unsigned int)counter->min_cycles,
               (unsigned int)counter->max_cycles,
               avg_time_us);
    }
}

/**
 * @brief 性能瓶颈检测
 * @param threshold_percent 阈值百分比（相对于总时间）
 */
void PERF_DetectBottlenecks(float threshold_percent) {
    if (!g_perf_analyzer.initialized || g_perf_analyzer.counter_count == 0) {
        return;
    }

    // 计算总周期数
    uint64_t total_cycles = 0;
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        total_cycles += g_perf_analyzer.counters[i].total_cycles;
    }

    if (total_cycles == 0) {
        return;
    }

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║           性能瓶颈分析 (阈值: %.1f%%)                      ║\n", 
           threshold_percent);
    printf("╠════════════════════════════════════════════════════════════╣\n");

    bool found_bottleneck = false;

    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        perf_counter_t *counter = &g_perf_analyzer.counters[i];

        if (counter->call_count == 0) {
            continue;
        }

        float percent = (float)(counter->total_cycles * 100.0) / total_cycles;

        if (percent >= threshold_percent) {
            found_bottleneck = true;

            uint32_t avg_cycles = (uint32_t)(counter->total_cycles / counter->call_count);
            float avg_time_us = (float)avg_cycles / g_perf_analyzer.cpu_freq_mhz;

            printf("║ [瓶颈] %-20s                              ║\n", counter->name);
            printf("║   占用时间: %.1f%%                                        ║\n", percent);
            printf("║   调用次数: %u                                           ║\n", 
                   (unsigned int)counter->call_count);
            printf("║   平均耗时: %.2f μs                                      ║\n", avg_time_us);
            printf("╠════════════════════════════════════════════════════════════╣\n");
        }
    }

    if (!found_bottleneck) {
        printf("║ 未检测到明显的性能瓶颈                                     ║\n");
        printf("╠════════════════════════════════════════════════════════════╣\n");
    }

    printf("║ 总周期数: %llu                                            ║\n", 
           (unsigned long long)total_cycles);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/**
 * @brief 性能对比分析
 * @param name1 第一个计数器名称
 * @param name2 第二个计数器名称
 */
void PERF_Compare(const char *name1, const char *name2) {
    perf_counter_t *counter1 = NULL;
    perf_counter_t *counter2 = NULL;

    // 查找计数器
    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        if (strcmp(g_perf_analyzer.counters[i].name, name1) == 0) {
            counter1 = &g_perf_analyzer.counters[i];
        }
        if (strcmp(g_perf_analyzer.counters[i].name, name2) == 0) {
            counter2 = &g_perf_analyzer.counters[i];
        }
    }

    if (counter1 == NULL || counter2 == NULL) {
        printf("[PERF] 错误: 未找到计数器\n");
        return;
    }

    if (counter1->call_count == 0 || counter2->call_count == 0) {
        printf("[PERF] 错误: 计数器无数据\n");
        return;
    }

    uint32_t avg1 = (uint32_t)(counter1->total_cycles / counter1->call_count);
    uint32_t avg2 = (uint32_t)(counter2->total_cycles / counter2->call_count);

    float time1_us = (float)avg1 / g_perf_analyzer.cpu_freq_mhz;
    float time2_us = (float)avg2 / g_perf_analyzer.cpu_freq_mhz;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  性能对比: %s vs %s\n", name1, name2);
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  %-20s │ %-20s\n", name1, name2);
    printf("───────────────────────────┼───────────────────────────\n");
    printf("  平均周期: %8u     │ 平均周期: %8u\n", 
           (unsigned int)avg1, (unsigned int)avg2);
    printf("  平均时间: %8.2f μs │ 平均时间: %8.2f μs\n", time1_us, time2_us);
    printf("  最小周期: %8u     │ 最小周期: %8u\n", 
           (unsigned int)counter1->min_cycles, (unsigned int)counter2->min_cycles);
    printf("  最大周期: %8u     │ 最大周期: %8u\n", 
           (unsigned int)counter1->max_cycles, (unsigned int)counter2->max_cycles);
    printf("  调用次数: %8u     │ 调用次数: %8u\n", 
           (unsigned int)counter1->call_count, (unsigned int)counter2->call_count);
    printf("═══════════════════════════════════════════════════════════\n");

    if (avg1 > avg2) {
        float speedup = (float)avg1 / avg2;
        printf("  %s 比 %s 慢 %.2fx\n", name1, name2, speedup);
    } else if (avg2 > avg1) {
        float speedup = (float)avg2 / avg1;
        printf("  %s 比 %s 快 %.2fx\n", name1, name2, speedup);
    } else {
        printf("  两者性能相当\n");
    }
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");
}

/**
 * @brief 性能趋势分析
 * @param name 计数器名称
 */
void PERF_AnalyzeTrend(const char *name) {
    perf_counter_t *counter = NULL;

    for (int i = 0; i < g_perf_analyzer.counter_count; i++) {
        if (strcmp(g_perf_analyzer.counters[i].name, name) == 0) {
            counter = &g_perf_analyzer.counters[i];
            break;
        }
    }

    if (counter == NULL || counter->call_count == 0) {
        printf("[PERF] 未找到计数器或无数据\n");
        return;
    }

    // 计算历史数据的统计信息
    uint32_t sum = 0;
    uint32_t count = 0;
    uint32_t min = 0xFFFFFFFF;
    uint32_t max = 0;

    for (int i = 0; i < PERF_HISTORY_SIZE; i++) {
        if (counter->history[i] > 0) {
            sum += counter->history[i];
            count++;
            if (counter->history[i] < min) min = counter->history[i];
            if (counter->history[i] > max) max = counter->history[i];
        }
    }

    if (count == 0) {
        printf("[PERF] 历史数据不足\n");
        return;
    }

    uint32_t avg = sum / count;

    // 计算标准差
    uint64_t variance_sum = 0;
    for (int i = 0; i < PERF_HISTORY_SIZE; i++) {
        if (counter->history[i] > 0) {
            int32_t diff = (int32_t)counter->history[i] - (int32_t)avg;
            variance_sum += (uint64_t)(diff * diff);
        }
    }

    float std_dev = sqrtf((float)variance_sum / count);
    float cv = (std_dev / avg) * 100.0f;  // 变异系数

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  性能趋势分析: %s\n", counter->name);
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  样本数量:     %u\n", count);
    printf("  平均值:       %u 周期\n", avg);
    printf("  标准差:       %.2f 周期\n", std_dev);
    printf("  变异系数:     %.2f%%\n", cv);
    printf("  最小值:       %u 周期\n", min);
    printf("  最大值:       %u 周期\n", max);
    printf("  范围:         %u 周期\n", max - min);
    printf("───────────────────────────────────────────────────────────\n");

    // 性能稳定性评估
    if (cv < 5.0f) {
        printf("  稳定性评估:   优秀 (变化很小)\n");
    } else if (cv < 10.0f) {
        printf("  稳定性评估:   良好 (变化较小)\n");
    } else if (cv < 20.0f) {
        printf("  稳定性评估:   一般 (有一定波动)\n");
    } else {
        printf("  稳定性评估:   较差 (波动较大)\n");
    }

    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");
}

