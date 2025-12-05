/**
 * @file warning_beep.h
 * @brief Warning beep module for audio alerts
 */

#ifndef WARNING_BEEP_H
#define WARNING_BEEP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Beep duration types
 */
typedef enum {
    BEEP_SHORT = 100,   // 100ms beep
    BEEP_LONG = 500     // 500ms beep
} beep_duration_t;

/**
 * @brief Initialize the warning beep system
 */
void warning_beep_init(void);

/**
 * @brief Play a single beep
 * @param duration Duration of the beep (BEEP_SHORT or BEEP_LONG)
 */
void warning_beep_play(beep_duration_t duration);

/**
 * @brief Start repeating beeps at a specified interval
 * @param duration Duration of each beep (BEEP_SHORT or BEEP_LONG)
 * @param interval_ms Interval between beeps in milliseconds (0 to stop)
 */
void warning_beep_repeat(beep_duration_t duration, uint32_t interval_ms);

/**
 * @brief Stop all repeating beeps
 */
void warning_beep_stop(void);

#endif // WARNING_BEEP_H
