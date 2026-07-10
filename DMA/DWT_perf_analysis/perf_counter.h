#ifndef PERF_COUNTER_H
#define PERF_COUNTER_H

#include <stdint.h>
#include <stdbool.h>

/* 配置参数 */
#define PERF_MAX_COUNTERS       32      // 最大计数器数量
#define PERF_NAME_MAX_LEN       32      // 名称最大长度
#define PERF_HISTORY_SIZE       10      // 历史记录数量

// DWT（数据观测与追踪单元）寄存器物理地址映射宏定义
// DWT 基地址：0xE0001000，所有寄存器均为32位硬件寄存器
#define DWT_CTRL                (*(volatile uint32_t *)0xE0001000)    // DWT控制寄存器，总开关、各类计数使能控制位
#define DWT_CYCCNT              (*(volatile uint32_t *)0xE0001004)    // DWT周期计数器，内核每时钟周期自增1，用于高精度代码耗时测量
#define DWT_CPICNT              (*(volatile uint32_t *)0xE0001008)    // CPI计数寄存器，统计每条指令平均消耗周期数
#define DWT_EXCCNT              (*(volatile uint32_t *)0xE000100C)    // 异常开销计数寄存器，统计中断/异常执行占用的总周期
#define DWT_SLEEPCNT            (*(volatile uint32_t *)0xE0001010)    // 休眠周期计数寄存器，统计内核进入休眠模式的总时钟周期
#define DWT_LSUCNT              (*(volatile uint32_t *)0xE0001014)    // 加载/存储指令计数寄存器，统计所有内存读写指令执行次数
#define DWT_FOLDCNT             (*(volatile uint32_t *)0xE0001018)    // 折叠指令计数寄存器，统计单周期并行执行的优化指令数量

// CoreDebug调试控制块 DEMCR寄存器映射
#define CoreDebug_DEMCR         (*(volatile uint32_t *)0xE000EDFC)    // 调试异常与监视器控制寄存器，全局追踪总开关
#define CoreDebug_DEMCR_TRCENA  (1 << 24)                              // DEMCR第24位掩码：全局追踪使能位，必须置1才能启用DWT/ITM/TPI硬件

/* DWT_CTRL寄存器各功能使能位掩码定义 */
#define DWT_CTRL_CYCCNTENA      (1 << 0)       // bit0：CYCCNT周期计数器使能开关，置1后周期计数器开始自增
#define DWT_CTRL_CPIEVTENA      (1 << 17)      // bit17：CPI计数事件使能，开启CPICNT硬件统计
#define DWT_CTRL_EXCEVTENA      (1 << 18)      // bit18：异常开销计数事件使能，开启EXCCNT硬件统计
#define DWT_CTRL_SLEEPEVTENA    (1 << 19)      // bit19：休眠周期计数事件使能，开启SLEEPCNT硬件统计
#define DWT_CTRL_LSUEVTENA      (1 << 20)      // bit20：加载/存储指令计数事件使能，开启LSUCNT硬件统计
#define DWT_CTRL_FOLDEVTENA     (1 << 21)      // bit21：折叠指令计数事件使能，开启FOLDCNT硬件统计
/**
 * @brief 性能计数器状态
 */
typedef enum {
    PERF_STATE_IDLE = 0,        // 空闲
    PERF_STATE_RUNNING,         // 运行中
    PERF_STATE_STOPPED,         // 已停止
    PERF_STATE_ERROR            // 错误
} perf_state_t;

/**
 * @brief 性能统计数据
 */
typedef struct {
    uint32_t cycles;            // 周期数
    uint32_t cpi_count;         // CPI计数
    uint32_t exc_count;         // 异常计数
    uint32_t sleep_count;       // 睡眠计数
    uint32_t lsu_count;         // LSU计数
    uint32_t fold_count;        // 折叠计数
    uint32_t timestamp;         // 时间戳
} perf_snapshot_t;

/**
 * @brief 性能计数器
 */
typedef struct {
    char name[PERF_NAME_MAX_LEN];       // 计数器名称
    perf_state_t state;                 // 状态
    perf_snapshot_t start;              // 开始快照
    perf_snapshot_t end;                // 结束快照
    uint32_t call_count;                // 调用次数
    uint64_t total_cycles;              // 总周期数
    uint32_t min_cycles;                // 最小周期数
    uint32_t max_cycles;                // 最大周期数
    uint32_t history[PERF_HISTORY_SIZE]; // 历史记录
    uint8_t history_index;              // 历史索引
    bool enabled;                       // 是否启用
} perf_counter_t;

/**
 * @brief 性能分析器
 */
typedef struct {
    perf_counter_t counters[PERF_MAX_COUNTERS];
    uint8_t counter_count;
    bool initialized;
    uint32_t cpu_freq_mhz;
} perf_analyzer_t;


void PERF_DWT_Reset(void);
int PERF_Init(uint32_t cpu_freq_mhz);
int PERF_Start(const char *name);
int PERF_Stop(const char *name);
int PERF_GetCycles(const char *name, uint32_t *cycles);
void PERF_Reset(const char *name);
void PERF_SetEnabled(const char *name, bool enabled);
void PERF_Report(void);
void PERF_ReportDetailed(const char *name);
void PERF_ReportJSON(void);
void PERF_ReportCSV(void);
void PERF_DetectBottlenecks(float threshold_percent);
void PERF_Compare(const char *name1, const char *name2);
void PERF_AnalyzeTrend(const char *name);
#endif /* PERF_COUNTER_H */


