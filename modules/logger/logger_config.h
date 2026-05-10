#ifndef LOGGER_CONFIG_H
#define LOGGER_CONFIG_H

/*
 * Compile-time logging controls.
 *
 * Set a tag to 0 to compile its log macro calls into no-ops. This keeps noisy
 * diagnostics available while making it cheap to silence whole subsystems.
 */

#define LOG_ENABLE_SYS       1
#define LOG_ENABLE_CAMERA    1
#define LOG_ENABLE_NN        1
#define LOG_ENABLE_POST      1
#define LOG_ENABLE_DISPLAY   1
#define LOG_ENABLE_USB       1
#define LOG_ENABLE_AI_RUNNER 1
#define LOG_ENABLE_DEBUG     0

/* Default rate limits in milliseconds. */
#define LOG_RATE_NONE        0U
#define LOG_RATE_FAST        50U
#define LOG_RATE_DEFAULT     100U
#define LOG_RATE_SLOW        500U

#define LOG_BUFFER_SIZE      384U

#endif /* LOGGER_CONFIG_H */
