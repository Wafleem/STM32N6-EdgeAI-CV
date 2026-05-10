#include "logger.h"

#include <stdarg.h>
#include <stdio.h>

#include "stm32n6xx_hal.h"

typedef struct
{
  uint32_t last_log_time;
  uint32_t interval_ms;
} LogRateLimiter_t;

static LogRateLimiter_t s_rate_limiters[LOG_TAG_COUNT];

static const char *const s_tag_names[LOG_TAG_COUNT] = {
  "SYS",
  "CAM",
  "NN",
  "POST",
  "DISP",
  "USB",
  "AIRUN",
  "DBG"
};

static const char *const s_level_names[] = {
  "ERROR",
  "WARN",
  "INFO",
  "TRACE",
  "CSV"
};

static bool Logger_ShouldLog(LogTag_t tag)
{
  if (tag >= LOG_TAG_COUNT)
  {
    return false;
  }

  if (s_rate_limiters[tag].interval_ms == LOG_RATE_NONE)
  {
    return true;
  }

  uint32_t now = HAL_GetTick();
  uint32_t elapsed = now - s_rate_limiters[tag].last_log_time;
  if (elapsed >= s_rate_limiters[tag].interval_ms)
  {
    s_rate_limiters[tag].last_log_time = now;
    return true;
  }

  return false;
}

void Logger_Init(void)
{
  for (uint32_t i = 0; i < LOG_TAG_COUNT; i++)
  {
    s_rate_limiters[i].last_log_time = 0U;
    s_rate_limiters[i].interval_ms = LOG_RATE_NONE;
  }
}

void Logger_SetRate(LogTag_t tag, uint32_t interval_ms)
{
  if (tag < LOG_TAG_COUNT)
  {
    s_rate_limiters[tag].interval_ms = interval_ms;
  }
}

uint32_t Logger_GetRate(LogTag_t tag)
{
  if (tag < LOG_TAG_COUNT)
  {
    return s_rate_limiters[tag].interval_ms;
  }

  return 0U;
}

void Logger_Log(LogTag_t tag, LogLevel_t level, const char *fmt, ...)
{
  if ((tag >= LOG_TAG_COUNT) || (level > LOG_LEVEL_CSV) || (fmt == NULL))
  {
    return;
  }

  if (!Logger_ShouldLog(tag))
  {
    return;
  }

  char buf[LOG_BUFFER_SIZE];
  int offset = snprintf(buf, sizeof(buf), "%s: [%s] ",
                        s_level_names[level], s_tag_names[tag]);
  if ((offset < 0) || (offset >= (int)sizeof(buf)))
  {
    return;
  }

  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(buf + offset, sizeof(buf) - (size_t)offset, fmt, args);
  va_end(args);

  if (written < 0)
  {
    return;
  }

  int total = offset + written;
  if (total < 0)
  {
    return;
  }

  if (total > ((int)sizeof(buf) - 3))
  {
    total = (int)sizeof(buf) - 3;
  }

  if ((total == 0) || (buf[total - 1] != '\n'))
  {
    buf[total++] = '\r';
    buf[total++] = '\n';
    buf[total] = '\0';
  }

  printf("%s", buf);
}

void Logger_CSV(LogTag_t tag, const char *fmt, ...)
{
  if ((tag >= LOG_TAG_COUNT) || (fmt == NULL))
  {
    return;
  }

  if (!Logger_ShouldLog(tag))
  {
    return;
  }

  char buf[LOG_BUFFER_SIZE];
  int offset = snprintf(buf, sizeof(buf), "%s,%lu,",
                        s_tag_names[tag], (unsigned long)HAL_GetTick());
  if ((offset < 0) || (offset >= (int)sizeof(buf)))
  {
    return;
  }

  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(buf + offset, sizeof(buf) - (size_t)offset, fmt, args);
  va_end(args);

  if (written < 0)
  {
    return;
  }

  int total = offset + written;
  if (total > ((int)sizeof(buf) - 3))
  {
    total = (int)sizeof(buf) - 3;
  }

  buf[total++] = '\r';
  buf[total++] = '\n';
  buf[total] = '\0';
  printf("%s", buf);
}
