#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Defines
#define LOX_EXIT_SUCCESS 0
#define LOX_EXIT_FAILURE 1
#define LOX_EXIT_IMPROPER_USAGE 2
#define LOX_EXIT_COMPILE_ERROR 65
#define LOX_EXIT_RUNTIME_ERROR 70
#define LOX_EXIT_FILE_ERROR 74

// Options
#define CLOX_ENABLE_NAN_BOXING
// #define CLOX_STRESS_GC
// #define CLOX_LOG_GC
// #define CLOX_PRINT_CODE
// #define CLOX_TRACE_EXECUTION

typedef int CloxExitCode;

#endif
