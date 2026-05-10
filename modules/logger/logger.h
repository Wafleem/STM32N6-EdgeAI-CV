#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <stdint.h>

#include "logger_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  LOG_TAG_SYS = 0,
  LOG_TAG_CAMERA,
  LOG_TAG_NN,
  LOG_TAG_POST,
  LOG_TAG_DISPLAY,
  LOG_TAG_USB,
  LOG_TAG_AI_RUNNER,
  LOG_TAG_DEBUG,
  LOG_TAG_COUNT
} LogTag_t;

typedef enum
{
  LOG_LEVEL_ERROR = 0,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_TRACE,
  LOG_LEVEL_CSV
} LogLevel_t;

void Logger_Init(void);
void Logger_SetRate(LogTag_t tag, uint32_t interval_ms);
uint32_t Logger_GetRate(LogTag_t tag);
void Logger_Log(LogTag_t tag, LogLevel_t level, const char *fmt, ...);
void Logger_CSV(LogTag_t tag, const char *fmt, ...);

#define LOG_TAG_ENABLED(tag) \
  (((tag) == LOG_TAG_SYS && LOG_ENABLE_SYS) || \
   ((tag) == LOG_TAG_CAMERA && LOG_ENABLE_CAMERA) || \
   ((tag) == LOG_TAG_NN && LOG_ENABLE_NN) || \
   ((tag) == LOG_TAG_POST && LOG_ENABLE_POST) || \
   ((tag) == LOG_TAG_DISPLAY && LOG_ENABLE_DISPLAY) || \
   ((tag) == LOG_TAG_USB && LOG_ENABLE_USB) || \
   ((tag) == LOG_TAG_AI_RUNNER && LOG_ENABLE_AI_RUNNER) || \
   ((tag) == LOG_TAG_DEBUG && LOG_ENABLE_DEBUG))

#define LOG_ERROR(tag, fmt, ...) \
  do { if (LOG_TAG_ENABLED(tag)) { Logger_Log((tag), LOG_LEVEL_ERROR, (fmt), ##__VA_ARGS__); } } while (0)

#define LOG_WARN(tag, fmt, ...) \
  do { if (LOG_TAG_ENABLED(tag)) { Logger_Log((tag), LOG_LEVEL_WARN, (fmt), ##__VA_ARGS__); } } while (0)

#define LOG_INFO(tag, fmt, ...) \
  do { if (LOG_TAG_ENABLED(tag)) { Logger_Log((tag), LOG_LEVEL_INFO, (fmt), ##__VA_ARGS__); } } while (0)

#define LOG_TRACE(tag, fmt, ...) \
  do { if (LOG_TAG_ENABLED(tag)) { Logger_Log((tag), LOG_LEVEL_TRACE, (fmt), ##__VA_ARGS__); } } while (0)

#define LOG_CSV(tag, fmt, ...) \
  do { if (LOG_TAG_ENABLED(tag)) { Logger_CSV((tag), (fmt), ##__VA_ARGS__); } } while (0)

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
