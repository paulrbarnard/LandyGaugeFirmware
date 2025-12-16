/**
 * @file warning_beep.h
 * @brief Warning beep module for audio alerts with MP3 playback
 */

#ifndef WARNING_BEEP_H
#define WARNING_BEEP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Warning level types
 */
typedef enum {
    WARNING_LEVEL_NONE = 0,
    WARNING_LEVEL_YELLOW,   // Warning - plays WarningRoll.mp3
    WARNING_LEVEL_RED       // Danger - plays DangerRoll.mp3
} warning_level_type_t;

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
 * @brief Start warning audio (beep + MP3) with 10 second repeat - for ROLL only
 * @param level Warning level (YELLOW for warning, RED for danger)
 */
void warning_beep_start(warning_level_type_t level);

/**
 * @brief Start pitch warning beeps (beeps only, no MP3)
 * @param level Warning level (YELLOW = every 3s, RED = every 0.5s)
 */
void warning_pitch_start(warning_level_type_t level);

/**
 * @brief Stop all warning audio (both roll and pitch)
 */
void warning_beep_stop(void);

#endif // WARNING_BEEP_H
