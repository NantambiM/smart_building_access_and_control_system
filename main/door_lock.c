#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_timer.h"

// ============================================
// DOOR CONFIGURATION
// ============================================

#define ACCESS_DURATION_US (5LL * 1000000LL)

// ============================================
// DOOR VARIABLES
// ============================================

static int64_t door_unlock_time = 0;
static bool door_unlocked = false;

// ============================================
// INITIALIZE DOOR LOCK
// ============================================

void door_lock_initialize(void)
{
    door_unlock_time = 0;
    door_unlocked = false;

    printf("[DOOR] Door lock initialized. Door LOCKED.\n");
}

// ============================================
// UNLOCK DOOR
// ============================================

void door_lock_unlock(void)
{
    door_unlock_time = esp_timer_get_time();

    door_unlocked = true;

    printf("[DOOR] Lock opened for 5 seconds.\n");
}

// ============================================
// MONITOR DOOR LOCK
// ============================================

void monitor_door_lock(void)
{
    if (door_unlocked)
    {
        int64_t elapsed =
            esp_timer_get_time() - door_unlock_time;

        if (elapsed >= ACCESS_DURATION_US)
        {
            door_unlocked = false;

            printf("[DOOR] Lock closed automatically.\n");
        }
    }
}

// ============================================
// DOOR STATUS
// ============================================

bool rfid_is_door_unlocked(void)
{
    return door_unlocked;
}