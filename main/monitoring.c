
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_timer.h"

// =====================================================
// PIR PIN DEFINITIONS
// =====================================================

#define PIR_ROOM1 GPIO_NUM_27
#define PIR_ROOM2 GPIO_NUM_26

// =====================================================
// CONFIGURATION
// =====================================================

#define STATUS_INTERVAL_US (30LL * 1000000LL)

// =====================================================
// RFID MODULE FUNCTION DECLARATIONS
// =====================================================

void rfid_display_user_status(void);

int rfid_get_occupancy_count(void);
int rfid_get_user_count(void);

uint32_t rfid_get_access_attempts(void);
uint32_t rfid_get_access_granted(void);
uint32_t rfid_get_access_denied(void);

bool rfid_is_door_unlocked(void);

// =====================================================
// ROOM MODULE FUNCTION DECLARATIONS
// =====================================================

bool rooms_is_monitoring_active(void);
bool rooms_is_room1_light_on(void);
bool rooms_is_room2_light_on(void);

// =====================================================
// MONITORING VARIABLES
// =====================================================

static int64_t last_status_display = 0;


// =====================================================
// SYSTEM MONITORING DASHBOARD
// =====================================================

void display_system_status(void)
{
    int64_t current_time = esp_timer_get_time();

    // Display dashboard every 30 seconds
    if (current_time - last_status_display < STATUS_INTERVAL_US)
    {
        return;
    }

    last_status_display = current_time;

    printf("\n");
    printf("====================================================\n");
    printf("              SMART BUILDING DASHBOARD              \n");
    printf("====================================================\n");


    // -------------------------------------------------
    // BUILDING OCCUPANCY
    // -------------------------------------------------

    printf("\n[BUILDING OCCUPANCY]\n");

    printf("People inside building: %d\n",
           rfid_get_occupancy_count());

    printf("Registered users: %d\n",
           rfid_get_user_count());


    // -------------------------------------------------
    // ACCESS STATISTICS
    // -------------------------------------------------

    printf("\n[ACCESS STATISTICS]\n");

    printf("Access attempts: %lu\n",
           (unsigned long)rfid_get_access_attempts());

    printf("Access granted: %lu\n",
           (unsigned long)rfid_get_access_granted());

    printf("Access denied: %lu\n",
           (unsigned long)rfid_get_access_denied());


    // -------------------------------------------------
    // REGISTERED USER STATUS
    // -------------------------------------------------

    printf("\n[REGISTERED USERS]\n");

    rfid_display_user_status();


    // -------------------------------------------------
    // ROOM MONITORING
    // -------------------------------------------------

    printf("\n[ROOM MONITORING]\n");

    printf("Room monitoring: %s\n",
           rooms_is_monitoring_active() ? "ACTIVE" : "INACTIVE");


    // -------------------------------------------------
    // ROOM 1
    // -------------------------------------------------

    printf("\nRoom 1:\n");

    printf("PIR motion: %s\n",
           gpio_get_level(PIR_ROOM1) ? "DETECTED" : "NOT DETECTED");

    printf("Room 1 light: %s\n",
           rooms_is_room1_light_on() ? "ON" : "OFF");


    // -------------------------------------------------
    // ROOM 2
    // -------------------------------------------------

    printf("\nRoom 2:\n");

    printf("PIR motion: %s\n",
           gpio_get_level(PIR_ROOM2) ? "DETECTED" : "NOT DETECTED");

    printf("Room 2 light: %s\n",
           rooms_is_room2_light_on() ? "ON" : "OFF");


    // -------------------------------------------------
    // DOOR LOCK STATUS
    // -------------------------------------------------

    printf("\n[DOOR LOCK STATUS]\n");

    printf("Door: %s\n",
           rfid_is_door_unlocked() ? "UNLOCKED" : "LOCKED");


    printf("\n====================================================\n");
    printf("             END OF SYSTEM STATUS                   \n");
    printf("====================================================\n\n");
}
