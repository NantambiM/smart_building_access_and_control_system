
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/* =====================================================
   FUNCTIONS FROM OTHER MODULES
   ===================================================== */

void door_lock_initialize(void);
void door_lock_unlock(void);

bool mfrc522_request(int reader_id);
bool mfrc522_read_uid(int reader_id, uint8_t *uid);

void sd_logger_log_event(const char *event, const char *description);
void sd_logger_display_logs(void);
bool sd_logger_is_ready(void);

bool sd_logger_save_schedule(
    int opening_hour,
    int opening_minute,
    int closing_hour,
    int closing_minute
);

bool sd_logger_load_schedule(
    int *opening_hour,
    int *opening_minute,
    int *closing_hour,
    int *closing_minute
);

bool rtc_get_datetime(
    int *year,
    int *month,
    int *date,
    int *day,
    int *hour,
    int *minute,
    int *second
);

/* RFID access feedback functions */
void access_feedback_success(void);
void access_feedback_failure(void);

/* =====================================================
   READER IDENTIFIERS
   ===================================================== */

#define ENTRY_READER 0
#define EXIT_READER  1

#define CARD_REMOVAL_MISSES 3

/* =====================================================
   ACCESS SCHEDULE DEFAULTS
   ===================================================== */

#define DEFAULT_OPEN_HOUR    7
#define DEFAULT_OPEN_MINUTE  0

#define DEFAULT_CLOSE_HOUR   18
#define DEFAULT_CLOSE_MINUTE 0

#define ADMIN_SESSION_US (5LL * 60LL * 1000000LL)

/* =====================================================
   AUTHORISED USER DATABASE
   ===================================================== */

typedef struct
{
    const char *name;
    uint8_t uid[4];
    bool inside;
} authorized_user_t;

static authorized_user_t authorized_users[] =
{
    {"User 1", {0x11, 0x22, 0x33, 0x44}, false},
    {"User 2", {0x01, 0x02, 0x03, 0x04}, false},
    {"User 3", {0x55, 0x66, 0x77, 0x88}, false},
    {"User 4", {0xAA, 0xBB, 0xCC, 0xDD}, false}
};

#define USER_COUNT \
    (sizeof(authorized_users) / sizeof(authorized_users[0]))

/* User 2 is the blue card administrator. */
#define ADMIN_USER_INDEX 1

/* =====================================================
   SYSTEM VARIABLES
   ===================================================== */

static int occupancy_count = 0;

static uint32_t access_attempts = 0;
static uint32_t access_granted = 0;
static uint32_t access_denied = 0;

static bool card_latched[2] = {false, false};
static uint8_t card_missed_scans[2] = {0, 0};

/* =====================================================
   ACCESS SCHEDULE VARIABLES
   ===================================================== */

static int opening_hour = DEFAULT_OPEN_HOUR;
static int opening_minute = DEFAULT_OPEN_MINUTE;

static int closing_hour = DEFAULT_CLOSE_HOUR;
static int closing_minute = DEFAULT_CLOSE_MINUTE;

/* =====================================================
   ADMINISTRATOR SESSION
   ===================================================== */

static bool admin_authenticated = false;
static int64_t admin_session_expiry = 0;


/* Check whether administrator session is still valid. */

bool rfid_is_admin_authenticated(void)
{
    if (!admin_authenticated)
    {
        return false;
    }

    if (esp_timer_get_time() >= admin_session_expiry)
    {
        admin_authenticated = false;
        admin_session_expiry = 0;

        printf("\n[ADMIN] Administrator session expired.\n");

        sd_logger_log_event(
            "ADMIN_SESSION_EXPIRED",
            "Administrator session expired"
        );

        return false;
    }

    return true;
}


/* Authenticate the administrator. */

static void authenticate_administrator(void)
{
    admin_authenticated = true;

    admin_session_expiry =
        esp_timer_get_time() + ADMIN_SESSION_US;

    printf("\n");
    printf("============================================\n");
    printf("       ADMINISTRATOR AUTHENTICATED          \n");
    printf("============================================\n");
    printf("Blue card recognized: User 2\n");
    printf("Administrator session valid for 5 minutes.\n");
    printf("You can now configure the access schedule.\n");
    printf("============================================\n");

    sd_logger_log_event(
        "ADMIN_AUTHENTICATED",
        "User 2 blue RFID card authenticated as administrator"
    );
}


/* End administrator session manually. */

void rfid_logout_admin(void)
{
    if (admin_authenticated)
    {
        admin_authenticated = false;
        admin_session_expiry = 0;

        printf("[ADMIN] Administrator logged out.\n");

        sd_logger_log_event(
            "ADMIN_LOGOUT",
            "Administrator session ended"
        );
    }
}


/* =====================================================
   ACCESS SCHEDULE FUNCTIONS
   ===================================================== */

/* Get the current configured schedule. */

void rfid_get_access_schedule(
    int *start_hour,
    int *start_minute,
    int *end_hour,
    int *end_minute)
{
    if (start_hour != NULL)
        *start_hour = opening_hour;

    if (start_minute != NULL)
        *start_minute = opening_minute;

    if (end_hour != NULL)
        *end_hour = closing_hour;

    if (end_minute != NULL)
        *end_minute = closing_minute;
}


/* Change schedule. Administrator authentication is required. */

bool rfid_set_access_schedule(
    int start_hour,
    int start_minute,
    int end_hour,
    int end_minute)
{
    if (!rfid_is_admin_authenticated())
    {
        printf("[SCHEDULE] Access denied. Administrator login required.\n");
        return false;
    }

    if (start_hour < 0 || start_hour > 23 ||
        end_hour < 0 || end_hour > 23 ||
        start_minute < 0 || start_minute > 59 ||
        end_minute < 0 || end_minute > 59)
    {
        printf("[SCHEDULE] Invalid time values.\n");
        return false;
    }

    int start_total = start_hour * 60 + start_minute;
    int end_total = end_hour * 60 + end_minute;

    if (start_total >= end_total)
    {
        printf("[SCHEDULE] Closing time must be later than opening time.\n");
        return false;
    }

    int old_opening_hour = opening_hour;
    int old_opening_minute = opening_minute;
    int old_closing_hour = closing_hour;
    int old_closing_minute = closing_minute;

    opening_hour = start_hour;
    opening_minute = start_minute;

    closing_hour = end_hour;
    closing_minute = end_minute;

    if (!sd_logger_save_schedule(
            opening_hour,
            opening_minute,
            closing_hour,
            closing_minute))
    {
        opening_hour = old_opening_hour;
        opening_minute = old_opening_minute;

        closing_hour = old_closing_hour;
        closing_minute = old_closing_minute;

        printf("[SCHEDULE] Update cancelled. Could not save to SD card.\n");

        return false;
    }

    printf("\n[SCHEDULE] Access schedule updated successfully.\n");

    printf(
        "New opening time: %02d:%02d\n",
        opening_hour,
        opening_minute
    );

    printf(
        "New closing time: %02d:%02d\n",
        closing_hour,
        closing_minute
    );

    char description[128];

    snprintf(
        description,
        sizeof(description),
        "Schedule changed to %02d:%02d - %02d:%02d by User 2",
        opening_hour,
        opening_minute,
        closing_hour,
        closing_minute
    );

    sd_logger_log_event(
        "SCHEDULE_UPDATED",
        description
    );

    return true;
}


/* Display current schedule. */

void rfid_display_access_schedule(void)
{
    printf("\n");
    printf("============================================\n");
    printf("             BUILDING SCHEDULE              \n");
    printf("============================================\n");

    printf(
        "Opening time: %02d:%02d\n",
        opening_hour,
        opening_minute
    );

    printf(
        "Closing time: %02d:%02d\n",
        closing_hour,
        closing_minute
    );

    printf("Time zone: East Africa Time (EAT)\n");

    printf("============================================\n");
}


/* Check whether building entry is currently allowed. */

static bool is_entry_time_allowed(void)
{
    int year, month, date, day;
    int hour, minute, second;

    if (!rtc_get_datetime(
            &year,
            &month,
            &date,
            &day,
            &hour,
            &minute,
            &second))
    {
        printf("[SCHEDULE] RTC time unavailable. Entry denied.\n");

        sd_logger_log_event(
            "ACCESS_DENIED",
            "Entry denied because RTC time could not be read"
        );

        return false;
    }

    int current_time = hour * 60 + minute;

    int opening_time =
        opening_hour * 60 + opening_minute;

    int closing_time =
        closing_hour * 60 + closing_minute;

    printf(
        "[SCHEDULE] Current time: %02d:%02d\n",
        hour,
        minute
    );

    if (current_time >= opening_time &&
        current_time < closing_time)
    {
        return true;
    }

    return false;
}


/* =====================================================
   LOG BUILDING OCCUPANCY
   ===================================================== */

static void log_occupancy_update(
    const char *user_name,
    const char *action)
{
    char description[160];

    snprintf(
        description,
        sizeof(description),
        "After %s %s; people inside building: %d",
        user_name,
        action,
        occupancy_count
    );

    sd_logger_log_event(
        "OCCUPANCY_UPDATE",
        description
    );

    printf(
        "[OCCUPANCY] Updated occupancy recorded: %d people inside.\n",
        occupancy_count
    );
}


/* =====================================================
   FIND AUTHORISED USER
   ===================================================== */

static authorized_user_t *find_authorized_user(uint8_t *uid)
{
    for (int i = 0; i < USER_COUNT; i++)
    {
        if (memcmp(uid, authorized_users[i].uid, 4) == 0)
        {
            return &authorized_users[i];
        }
    }

    return NULL;
}


/* =====================================================
   DISPLAY REGISTERED USERS
   ===================================================== */

void rfid_display_user_status(void)
{
    printf("\n========================================\n");
    printf("          REGISTERED USERS              \n");
    printf("========================================\n");

    for (int i = 0; i < USER_COUNT; i++)
    {
        printf(
            "%s | UID: %02X:%02X:%02X:%02X | Status: %s",
            authorized_users[i].name,
            authorized_users[i].uid[0],
            authorized_users[i].uid[1],
            authorized_users[i].uid[2],
            authorized_users[i].uid[3],
            authorized_users[i].inside ? "INSIDE" : "OUTSIDE"
        );

        if (i == ADMIN_USER_INDEX)
        {
            printf(" | ADMIN");
        }

        printf("\n");
    }

    printf("----------------------------------------\n");
    printf("Building occupancy: %d\n", occupancy_count);
    printf("========================================\n");
}


/* =====================================================
   PROCESS ENTRY OR EXIT READER
   ===================================================== */

static void process_reader(int reader_id)
{
    bool is_entry = (reader_id == ENTRY_READER);

    const char *reader_name =
        is_entry ? "ENTRY" : "EXIT";

    /* Check whether a card is currently detected. */

    if (!mfrc522_request(reader_id))
    {
        if (card_latched[reader_id])
        {
            card_missed_scans[reader_id]++;

            if (card_missed_scans[reader_id] >= CARD_REMOVAL_MISSES)
            {
                card_latched[reader_id] = false;
                card_missed_scans[reader_id] = 0;

                printf(
                    "[%s] Card removed. Ready for next card.\n",
                    reader_name
                );
            }
        }
        else
        {
            card_missed_scans[reader_id] = 0;
        }

        return;
    }

    card_missed_scans[reader_id] = 0;

    /* Do not process the same card repeatedly. */

    if (card_latched[reader_id])
    {
        return;
    }

    card_latched[reader_id] = true;

    printf("\n----------------------------------------\n");
    printf("[%s] RFID card detected.\n", reader_name);

    uint8_t uid[4] = {0};

    if (!mfrc522_read_uid(reader_id, uid))
    {
        printf("[ACCESS DENIED] Could not read card UID.\n");

        access_attempts++;
        access_denied++;

        access_feedback_failure();

        sd_logger_log_event(
            "ACCESS_DENIED",
            "Card UID could not be read"
        );

        return;
    }

    access_attempts++;

    printf(
        "UID: %02X:%02X:%02X:%02X\n",
        uid[0], uid[1], uid[2], uid[3]
    );

    authorized_user_t *user = find_authorized_user(uid);

    /* Reject unknown cards. */

    if (user == NULL)
    {
        printf("[ACCESS DENIED] Unknown RFID card.\n");

        access_denied++;

        access_feedback_failure();

        sd_logger_log_event(
            "ACCESS_DENIED",
            "Unknown RFID card"
        );

        return;
    }

    printf("User: %s\n", user->name);

    /* Authenticate blue card as administrator. */

    bool is_admin = (user == &authorized_users[ADMIN_USER_INDEX]);

    if (is_admin)
    {
        authenticate_administrator();
    }

    /* =================================================
       ENTRY READER
       ================================================= */

    if (is_entry)
    {
        /* Check schedule before allowing entry. */

        if (!is_entry_time_allowed())
        {
            printf(
                "[ACCESS DENIED] Building is closed. "
                "Entry allowed from %02d:%02d to %02d:%02d.\n",
                opening_hour,
                opening_minute,
                closing_hour,
                closing_minute
            );

            access_denied++;

            access_feedback_failure();

            char description[128];

            snprintf(
                description,
                sizeof(description),
                "%s denied entry outside schedule",
                user->name
            );

            sd_logger_log_event(
                "ACCESS_DENIED",
                description
            );

            return;
        }

        if (user->inside)
        {
            printf(
                "[ACCESS DENIED] %s is already inside.\n",
                user->name
            );

            access_denied++;

            access_feedback_failure();

            sd_logger_log_event(
                "ACCESS_DENIED",
                "User attempted duplicate entry"
            );

            return;
        }

        user->inside = true;
        occupancy_count++;

        access_granted++;

        printf("[ACCESS GRANTED] ENTRY.\n");
        printf("Welcome %s!\n", user->name);

        access_feedback_success();

        door_lock_unlock();

        printf(
            "Building occupancy: %d\n",
            occupancy_count
        );

        char description[100];

        snprintf(
            description,
            sizeof(description),
            "%s entered building",
            user->name
        );

        sd_logger_log_event(
            "ENTRY_GRANTED",
            description
        );

        log_occupancy_update(user->name, "entered");

        rfid_display_user_status();

        sd_logger_display_logs();
    }

    /* =================================================
       EXIT READER
       ================================================= */

    else
    {
        /*
         * Exit is deliberately not restricted by the
         * building schedule to preserve safe egress.
         */

        if (!user->inside)
        {
            printf(
                "[ACCESS DENIED] %s is not recorded inside.\n",
                user->name
            );

            access_denied++;

            access_feedback_failure();

            sd_logger_log_event(
                "ACCESS_DENIED",
                "User attempted exit while recorded outside"
            );

            return;
        }

        user->inside = false;

        if (occupancy_count > 0)
        {
            occupancy_count--;
        }

        access_granted++;

        printf("[ACCESS GRANTED] EXIT.\n");
        printf("Goodbye %s!\n", user->name);

        access_feedback_success();

        door_lock_unlock();

        printf(
            "Building occupancy: %d\n",
            occupancy_count
        );

        char description[100];

        snprintf(
            description,
            sizeof(description),
            "%s exited building",
            user->name
        );

        sd_logger_log_event(
            "EXIT_GRANTED",
            description
        );

        log_occupancy_update(user->name, "exited");

        rfid_display_user_status();

        sd_logger_display_logs();
    }

    printf("----------------------------------------\n");
}


/* =====================================================
   INITIALIZE ACCESS CONTROL
   ===================================================== */

void rfid_initialize(void)
{
    printf("\nInitializing access control...\n");

    occupancy_count = 0;

    access_attempts = 0;
    access_granted = 0;
    access_denied = 0;

    for (int i = 0; i < USER_COUNT; i++)
    {
        authorized_users[i].inside = false;
    }

    card_latched[ENTRY_READER] = false;
    card_latched[EXIT_READER] = false;

    card_missed_scans[ENTRY_READER] = 0;
    card_missed_scans[EXIT_READER] = 0;

    /* Start with default schedule. */

    opening_hour = DEFAULT_OPEN_HOUR;
    opening_minute = DEFAULT_OPEN_MINUTE;

    closing_hour = DEFAULT_CLOSE_HOUR;
    closing_minute = DEFAULT_CLOSE_MINUTE;

    /* Load saved schedule from SD card if available. */

    if (sd_logger_load_schedule(
            &opening_hour,
            &opening_minute,
            &closing_hour,
            &closing_minute))
    {
        printf("[SCHEDULE] Previous schedule restored from SD card.\n");
    }
    else
    {
        printf("[SCHEDULE] Using default schedule: 07:00 - 18:00.\n");
    }

    admin_authenticated = false;
    admin_session_expiry = 0;

    door_lock_initialize();

    printf("Administrator card: User 2 (Blue).\n");
    printf("Access control initialized.\n");
}


/* =====================================================
   MONITOR BOTH RFID READERS
   ===================================================== */

void rfid_monitor(void)
{
    process_reader(ENTRY_READER);
    process_reader(EXIT_READER);
}


/* =====================================================
   ACCESS CONTROL GETTERS
   ===================================================== */

int rfid_get_occupancy_count(void)
{
    return occupancy_count;
}

int rfid_get_user_count(void)
{
    return USER_COUNT;
}

uint32_t rfid_get_access_attempts(void)
{
    return access_attempts;
}

uint32_t rfid_get_access_granted(void)
{
    return access_granted;
}

uint32_t rfid_get_access_denied(void)
{
    return access_denied;
}
