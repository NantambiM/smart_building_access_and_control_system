
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// =====================================================
// RFID MODULE
// =====================================================

void rfid_initialize(void);
void rfid_monitor(void);

void mfrc522_spi_initialize(void);
void mfrc522_readers_initialize(void);

void monitor_door_lock(void);

int rfid_get_occupancy_count(void);
int rfid_get_user_count(void);
void rfid_display_user_status(void);

bool rfid_is_admin_authenticated(void);
void rfid_logout_admin(void);

void access_feedback_initialize(void);
void access_feedback_success(void);
void access_feedback_failure(void);

bool rfid_set_access_schedule(
    int start_hour,
    int start_minute,
    int end_hour,
    int end_minute
);

void rfid_get_access_schedule(
    int *start_hour,
    int *start_minute,
    int *end_hour,
    int *end_minute
);

void rfid_display_access_schedule(void);


// =====================================================
// SD CARD LOGGER MODULE
// =====================================================

bool sd_logger_initialize(void);
void sd_logger_display_logs(void);


// =====================================================
// ROOM MODULE
// =====================================================

void rooms_initialize(void);
void manage_room_monitoring(void);


// =====================================================
// MONITORING MODULE
// =====================================================

void display_system_status(void);


// =====================================================
// RTC MODULE - DS1307
// =====================================================

bool rtc_initialize(void);

bool rtc_get_datetime(
    int *year,
    int *month,
    int *date,
    int *day,
    int *hour,
    int *minute,
    int *second
);


// =====================================================
// SERIAL INPUT HELPER
// =====================================================

static void read_serial_line(char *buffer, size_t size)
{
    size_t position = 0;

    while (1)
    {
        int received = getchar();

        if (received == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        char input = (char)received;

        if (input == '\n' || input == '\r')
        {
            if (position > 0)
            {
                break;
            }

            continue;
        }

        if (input == '\b' || input == 127)
        {
            if (position > 0)
            {
                position--;
            }

            continue;
        }

        if (position < size - 1)
        {
            buffer[position++] = input;
        }
    }

    buffer[position] = '\0';
}


// =====================================================
// VALIDATE AND CONVERT HH:MM
// =====================================================

static bool parse_time(
    const char *input,
    int *hour,
    int *minute)
{
    if (strlen(input) != 5)
    {
        return false;
    }

    if (input[2] != ':')
    {
        return false;
    }

    if (input[0] < '0' || input[0] > '9' ||
        input[1] < '0' || input[1] > '9' ||
        input[3] < '0' || input[3] > '9' ||
        input[4] < '0' || input[4] > '9')
    {
        return false;
    }

    *hour =
        (input[0] - '0') * 10 +
        (input[1] - '0');

    *minute =
        (input[3] - '0') * 10 +
        (input[4] - '0');

    if (*hour > 23 || *minute > 59)
    {
        return false;
    }

    return true;
}


// =====================================================
// DISPLAY SERIAL COMMAND MENU
// =====================================================

static void display_command_menu(void)
{
    printf("\n");
    printf("============================================\n");
    printf("          SMART BUILDING MENU               \n");
    printf("============================================\n");
    printf("1 - View SD card access logs\n");
    printf("2 - View system dashboard\n");
    printf("3 - View registered users\n");
    printf("4 - View building occupancy\n");
    printf("5 - Display this menu\n");
    printf("6 - View building access schedule\n");
    printf("7 - Change access schedule (Admin only)\n");
    printf("8 - Administrator logout\n");
    printf("============================================\n");
    printf("Enter a command and press ENTER: ");
}


// =====================================================
// CHANGE BUILDING ACCESS SCHEDULE
// =====================================================

static void change_access_schedule(void)
{
    if (!rfid_is_admin_authenticated())
    {
        printf("\n");
        printf("[ADMIN] ACCESS DENIED.\n");
        printf("Scan the blue administrator card first.\n");
        return;
    }

    char opening_input[8];
    char closing_input[8];

    int start_hour, start_minute;
    int end_hour, end_minute;

    printf("\n");
    printf("============================================\n");
    printf("          CHANGE ACCESS SCHEDULE            \n");
    printf("============================================\n");

    printf("Enter opening time (HH:MM): ");

    read_serial_line(opening_input, sizeof(opening_input));

    if (!parse_time(
            opening_input,
            &start_hour,
            &start_minute))
    {
        printf("[SCHEDULE] Invalid opening time.\n");
        printf("Use HH:MM format, for example 07:00.\n");
        return;
    }

    printf("Enter closing time (HH:MM): ");

    read_serial_line(closing_input, sizeof(closing_input));

    if (!parse_time(
            closing_input,
            &end_hour,
            &end_minute))
    {
        printf("[SCHEDULE] Invalid closing time.\n");
        printf("Use HH:MM format, for example 18:00.\n");
        return;
    }

    if (rfid_set_access_schedule(
            start_hour,
            start_minute,
            end_hour,
            end_minute))
    {
        printf("\n[SCHEDULE] New schedule saved successfully.\n");
        rfid_display_access_schedule();
    }
    else
    {
        printf("\n[SCHEDULE] Schedule update failed.\n");
    }
}


// =====================================================
// PROCESS SERIAL COMMAND
// =====================================================

static void process_serial_command(const char *command)
{
    printf("\n");

    if (strcmp(command, "1") == 0)
    {
        sd_logger_display_logs();
    }
    else if (strcmp(command, "2") == 0)
    {
        display_system_status();
    }
    else if (strcmp(command, "3") == 0)
    {
        rfid_display_user_status();
    }
    else if (strcmp(command, "4") == 0)
    {
        printf("============================================\n");
        printf("          BUILDING OCCUPANCY                \n");
        printf("============================================\n");

        printf("People currently inside: %d\n",
               rfid_get_occupancy_count());

        printf("============================================\n");
    }
    else if (strcmp(command, "5") == 0)
    {
        display_command_menu();
    }
    else if (strcmp(command, "6") == 0)
    {
        rfid_display_access_schedule();
    }
    else if (strcmp(command, "7") == 0)
    {
        change_access_schedule();
    }
    else if (strcmp(command, "8") == 0)
    {
        rfid_logout_admin();
    }
    else
    {
        printf("[MENU] Invalid command. Enter 5 to view commands.\n");
    }

    printf("\nEnter a command and press ENTER: ");
}


// =====================================================
// SERIAL INPUT TASK
// =====================================================

static void serial_command_task(void *pvParameters)
{
    char command[16];

    display_command_menu();

    while (1)
    {
        read_serial_line(command, sizeof(command));

        process_serial_command(command);
    }
}


// =====================================================
// MAIN APPLICATION
// =====================================================

void app_main(void)
{
    printf("\n");
    printf("====================================================\n");
    printf("       SMART BUILDING CONTROL SYSTEM                \n");
    printf("       CoCIS - Makerere University                  \n");
    printf("====================================================\n");


    // -------------------------------------------------
    // STEP 1: INITIALIZE ROOM MONITORING
    // -------------------------------------------------

    printf("\nInitializing room monitoring...\n");

    rooms_initialize();


    // -------------------------------------------------
    // STEP 2: INITIALIZE SHARED SPI BUS
    // -------------------------------------------------

    printf("\nInitializing shared SPI bus...\n");

    mfrc522_spi_initialize();


    // -------------------------------------------------
    // STEP 3: INITIALIZE SD CARD
    // -------------------------------------------------

    printf("\nInitializing SD card...\n");

    bool sd_status = sd_logger_initialize();

    if (sd_status)
    {
        printf("[MAIN] SD card logging is available.\n");
    }
    else
    {
        printf("[MAIN] WARNING: SD card unavailable.\n");
        printf("[MAIN] System will continue without SD logging.\n");
    }


    // -------------------------------------------------
    // STEP 4: INITIALIZE RTC - DS1307
    // -------------------------------------------------

    printf("\nInitializing RTC...\n");

    bool rtc_status = rtc_initialize();

    if (rtc_status)
    {
        printf("[RTC] Communication initialized successfully.\n");

        int year, month, date;
        int day, hour, minute, second;

        if (rtc_get_datetime(
                &year,
                &month,
                &date,
                &day,
                &hour,
                &minute,
                &second))
        {
            printf("\n");
            printf("============================================\n");
            printf("          RTC CURRENT DATE AND TIME         \n");
            printf("============================================\n");

            printf("Date: %04d-%02d-%02d\n",
                   year, month, date);

            printf("Time: %02d:%02d:%02d\n",
                   hour, minute, second);

            printf("============================================\n");

            printf("[RTC] Clock is running successfully.\n");
        }
        else
        {
            printf("[RTC ERROR] Failed to read date and time.\n");
        }
    }
    else
    {
        printf("[RTC] Initialization failed.\n");
        printf("[RTC] Schedule enforcement will not be available.\n");
    }

    // access feedback
    printf("\nInitializing access feedback system...\n");

    access_feedback_initialize();

    printf("[MAIN] Access feedback system ready.\n");

    // -------------------------------------------------
    // STEP 5: INITIALIZE RFID AND DOOR LOCK
    // -------------------------------------------------

    printf("\nInitializing RFID system and door lock...\n");

    rfid_initialize();

    mfrc522_readers_initialize();


    // -------------------------------------------------
    // STEP 6: DISPLAY INITIAL INFORMATION
    // -------------------------------------------------

    printf("\n");
    printf("====================================================\n");
    printf("          SYSTEM INITIALIZATION COMPLETE           \n");
    printf("====================================================\n");

    printf("Registered users: %d\n",
           rfid_get_user_count());

    printf("Initial building occupancy: %d\n",
           rfid_get_occupancy_count());

    printf("SD card status: %s\n",
           sd_status ? "READY" : "UNAVAILABLE");

    printf("RTC status: %s\n",
           rtc_status ? "READY" : "UNAVAILABLE");

    printf("\nWaiting for RFID cards...\n");


    // -------------------------------------------------
    // STEP 7: START SERIAL COMMAND TASK
    // -------------------------------------------------

    printf("\nStarting serial command menu...\n");

    BaseType_t task_result = xTaskCreate(
        serial_command_task,
        "serial_command_task",
        4096,
        NULL,
        1,
        NULL
    );

    if (task_result != pdPASS)
    {
        printf("[MAIN] ERROR: Could not create serial command task.\n");
    }


    // -------------------------------------------------
    // STEP 8: MAIN SYSTEM LOOP
    // -------------------------------------------------

    while (1)
    {
        rfid_monitor();

        manage_room_monitoring();

        monitor_door_lock();

        display_system_status();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
