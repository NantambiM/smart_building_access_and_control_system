
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"

#define MOUNT_POINT   "/sdcard"
#define LOG_FILE      "/sdcard/ACCESS.csv"
#define SCHEDULE_FILE "/sdcard/SCHEDULE.txt"
#define SD_CS_PIN     GPIO_NUM_13

static sdmmc_card_t *sd_card = NULL;
static bool sd_ready = false;

/* =====================================================
   INITIALIZE SD CARD
   ===================================================== */

bool sd_logger_initialize(void)
{
    printf("\n[SD] Mounting memory card...\n");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    esp_err_t result = esp_vfs_fat_sdspi_mount(
        MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &sd_card
    );

    if (result != ESP_OK)
    {
        printf("[SD] Mount failed: %s\n", esp_err_to_name(result));
        sd_ready = false;
        return false;
    }

    printf("[SD] Card mounted successfully.\n");
    sdmmc_card_print_info(stdout, sd_card);

    struct stat st;

    if (stat(MOUNT_POINT, &st) != 0)
    {
        printf("[SD] Mount directory is not accessible.\n");
        sd_ready = false;
        return false;
    }

    printf("[SD] Mount directory accessible.\n");

    FILE *file = fopen(LOG_FILE, "a");

    if (file == NULL)
    {
        printf("[SD] Could not open ACCESS.csv: %s\n", strerror(errno));
        sd_ready = false;
        return false;
    }

    if (ftell(file) == 0)
    {
        fprintf(file, "Timestamp_us,Event,Description\n");
        fflush(file);
    }

    fclose(file);

    sd_ready = true;

    printf("[SD] Logging ready: %s\n", LOG_FILE);

    return true;
}

/* =====================================================
   SAVE ACCESS SCHEDULE
   ===================================================== */

bool sd_logger_save_schedule(
    int opening_hour,
    int opening_minute,
    int closing_hour,
    int closing_minute)
{
    if (!sd_ready)
    {
        printf("[SD] Cannot save schedule: SD card unavailable.\n");
        return false;
    }

    FILE *file = fopen(SCHEDULE_FILE, "w");

    if (file == NULL)
    {
        printf("[SD] Could not open schedule file: %s\n", strerror(errno));
        return false;
    }

    int written = fprintf(
        file,
        "%d %d %d %d\n",
        opening_hour,
        opening_minute,
        closing_hour,
        closing_minute
    );

    bool success = (written > 0);

    if (fflush(file) != 0)
    {
        success = false;
    }

    if (fclose(file) != 0)
    {
        success = false;
    }

    if (!success)
    {
        printf("[SD] Failed to write schedule file.\n");
        return false;
    }

    printf(
        "[SD] Schedule saved: %02d:%02d - %02d:%02d\n",
        opening_hour,
        opening_minute,
        closing_hour,
        closing_minute
    );

    return true;
}

/* =====================================================
   LOAD ACCESS SCHEDULE
   ===================================================== */

bool sd_logger_load_schedule(
    int *opening_hour,
    int *opening_minute,
    int *closing_hour,
    int *closing_minute)
{
    if (!sd_ready)
    {
        printf("[SD] Cannot load schedule: SD card unavailable.\n");
        return false;
    }

    if (opening_hour == NULL ||
        opening_minute == NULL ||
        closing_hour == NULL ||
        closing_minute == NULL)
    {
        printf("[SD] Invalid schedule output pointers.\n");
        return false;
    }

    FILE *file = fopen(SCHEDULE_FILE, "r");

    if (file == NULL)
    {
        printf("[SD] No saved schedule found. Using defaults.\n");
        return false;
    }

    int open_h, open_m, close_h, close_m;

    int fields_read = fscanf(
        file,
        "%d %d %d %d",
        &open_h,
        &open_m,
        &close_h,
        &close_m
    );

    fclose(file);

    if (fields_read != 4)
    {
        printf("[SD] Saved schedule file is invalid. Using defaults.\n");
        return false;
    }

    if (open_h < 0 || open_h > 23 ||
        close_h < 0 || close_h > 23 ||
        open_m < 0 || open_m > 59 ||
        close_m < 0 || close_m > 59 ||
        (open_h * 60 + open_m) >= (close_h * 60 + close_m))
    {
        printf("[SD] Saved schedule contains invalid times. Using defaults.\n");
        return false;
    }

    *opening_hour = open_h;
    *opening_minute = open_m;
    *closing_hour = close_h;
    *closing_minute = close_m;

    printf(
        "[SD] Saved schedule loaded: %02d:%02d - %02d:%02d\n",
        open_h,
        open_m,
        close_h,
        close_m
    );

    return true;
}

/* =====================================================
   WRITE EVENT TO CSV FILE
   ===================================================== */

void sd_logger_log_event(const char *event, const char *description)
{
    if (!sd_ready)
    {
        printf("[SD] Cannot log event: SD card unavailable.\n");
        return;
    }

    FILE *file = fopen(LOG_FILE, "a");

    if (file == NULL)
    {
        printf("[SD] Failed to open log file: %s\n", strerror(errno));
        return;
    }

    int64_t timestamp = esp_timer_get_time();

    char safe_description[160];

    snprintf(
        safe_description,
        sizeof(safe_description),
        "%s",
        description ? description : ""
    );

    for (int i = 0; safe_description[i] != '\0'; i++)
    {
        if (safe_description[i] == ',')
        {
            safe_description[i] = ';';
        }
    }

    fprintf(
        file,
        "%lld,%s,%s\n",
        (long long)timestamp,
        event ? event : "UNKNOWN",
        safe_description
    );

    fflush(file);
    fclose(file);

    printf("[SD] Event saved: %s\n", event ? event : "UNKNOWN");
}

/* =====================================================
   DISPLAY CSV CONTENTS IN SERIAL MONITOR
   ===================================================== */

void sd_logger_display_logs(void)
{
    if (!sd_ready)
    {
        printf("[SD] Cannot display logs. SD card unavailable.\n");
        return;
    }

    FILE *file = fopen(LOG_FILE, "r");

    if (file == NULL)
    {
        printf("[SD] Cannot open ACCESS.csv: %s\n", strerror(errno));
        return;
    }

    char line[256];

    printf("\n========================================\n");
    printf("          SD CARD ACCESS LOGS           \n");
    printf("========================================\n");

    while (fgets(line, sizeof(line), file) != NULL)
    {
        printf("%s", line);
    }

    printf("========================================\n");
    printf("          END OF ACCESS LOGS            \n");
    printf("========================================\n\n");

    fclose(file);
}

/* =====================================================
   SD CARD STATUS
   ===================================================== */

bool sd_logger_is_ready(void)
{
    return sd_ready;
}
