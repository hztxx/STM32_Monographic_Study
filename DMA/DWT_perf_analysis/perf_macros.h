/**
 * @file perf_macros.h
 * @brief 性能测量便捷宏
 */

#ifndef PERF_MACROS_H
#define PERF_MACROS_H

#include "perf_counter.h"

/* 性能测量宏 */
#define PERF_MEASURE_START(name)    PERF_Start(#name)
#define PERF_MEASURE_STOP(name)     PERF_Stop(#name)

/* 函数性能测量宏 */
#define PERF_FUNCTION_START()       PERF_Start(__FUNCTION__)
#define PERF_FUNCTION_STOP()        PERF_Stop(__FUNCTION__)

/* 代码块性能测量 */
#define PERF_BLOCK_START(name)      do { PERF_Start(name);
#define PERF_BLOCK_END(name)        PERF_Stop(name); } while(0)

/* 自动性能测量（使用作用域） */
typedef struct {
    const char *name;
} perf_scope_t;

static inline perf_scope_t perf_scope_begin(const char *name) {
    PERF_Start(name);
    perf_scope_t scope = {name};
    return scope;
}

static inline void perf_scope_end(perf_scope_t *scope) {
    if (scope && scope->name) {
        PERF_Stop(scope->name);
    }
}

#define PERF_SCOPE(name) \
    perf_scope_t __perf_scope_##name __attribute__((cleanup(perf_scope_end))) = \
        perf_scope_begin(#name)

/* 条件性能测量 */
#ifdef ENABLE_PERF_MEASURE
    #define PERF_MEASURE(name, code) \
        do { \
            PERF_Start(name); \
            code; \
            PERF_Stop(name); \
        } while(0)
#else
    #define PERF_MEASURE(name, code) do { code; } while(0)
#endif

#endif /* PERF_MACROS_H */