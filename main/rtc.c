
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/i2c.h"
#include "esp_err.h"

// =====================================================
// DS1307 CONFIGURATION
// =====================================================

#define RTC_I2C_PORT       I2C_NUM_0
#define RTC_ADDRESS        0x68

#define RTC_SDA_PIN        GPIO_NUM_32
#define RTC_SCL_PIN        GPIO_NUM_33

#define RTC_FREQUENCY      100000

static bool rtc_initialized = false;


// =====================================================
// BCD CONVERSION
// =====================================================

static uint8_t decimal_to_bcd(uint8_t value)
{
    return ((value / 10) << 4) | (value % 10);
}

static uint8_t bcd_to_decimal(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}


// =====================================================
// INITIALIZE DS1307
// =====================================================

bool rtc_initialize(void)
{
    printf("\n[RTC] Initializing DS1307...\n");

    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = RTC_SDA_PIN,
        .scl_io_num = RTC_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = RTC_FREQUENCY,
        .clk_flags = 0
    };

    esp_err_t result = i2c_param_config(
        RTC_I2C_PORT,
        &config
    );

    if (result != ESP_OK)
    {
        printf("[RTC] Configuration failed: %s\n",
               esp_err_to_name(result));
        return false;
    }

    result = i2c_driver_install(
        RTC_I2C_PORT,
        I2C_MODE_MASTER,
        0,
        0,
        0
    );

    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
    {
        printf("[RTC] Driver installation failed: %s\n",
               esp_err_to_name(result));
        return false;
    }

    rtc_initialized = true;

    printf("[RTC] DS1307 initialized successfully.\n");

    return true;
}


// =====================================================
// SET RTC DATE AND TIME
// =====================================================

bool rtc_set_datetime(
    int year,
    int month,
    int date,
    int day,
    int hour,
    int minute,
    int second
)
{
    if (!rtc_initialized)
    {
        printf("[RTC] ERROR: RTC not initialized.\n");
        return false;
    }

    if (year < 2000 || year > 2099 ||
        month < 1 || month > 12 ||
        date < 1 || date > 31 ||
        day < 1 || day > 7 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 59)
    {
        printf("[RTC] ERROR: Invalid date or time.\n");
        return false;
    }

    uint8_t data[8];

    data[0] = 0x00; // Starting register

    data[1] = decimal_to_bcd(second);
    data[2] = decimal_to_bcd(minute);
    data[3] = decimal_to_bcd(hour);
    data[4] = decimal_to_bcd(day);
    data[5] = decimal_to_bcd(date);
    data[6] = decimal_to_bcd(month);
    data[7] = decimal_to_bcd(year - 2000);

    esp_err_t result = i2c_master_write_to_device(
        RTC_I2C_PORT,
        RTC_ADDRESS,
        data,
        sizeof(data),
        pdMS_TO_TICKS(1000)
    );

    if (result != ESP_OK)
    {
        printf("[RTC] Failed to set time: %s\n",
               esp_err_to_name(result));
        return false;
    }

    printf("[RTC] Date and time updated successfully.\n");

    return true;
}


// =====================================================
// READ RTC DATE AND TIME
// =====================================================

bool rtc_get_datetime(
    int *year,
    int *month,
    int *date,
    int *day,
    int *hour,
    int *minute,
    int *second
)
{
    if (!rtc_initialized)
    {
        printf("[RTC] ERROR: RTC not initialized.\n");
        return false;
    }

    uint8_t register_address = 0x00;
    uint8_t data[7];

    esp_err_t result = i2c_master_write_read_device(
        RTC_I2C_PORT,
        RTC_ADDRESS,
        &register_address,
        1,
        data,
        sizeof(data),
        pdMS_TO_TICKS(1000)
    );

    if (result != ESP_OK)
    {
        printf("[RTC] Failed to read time: %s\n",
               esp_err_to_name(result));
        return false;
    }

    // CH bit indicates that the oscillator is stopped.
    if (data[0] & 0x80)
    {
        printf("[RTC] Clock is stopped. Set the time first.\n");
        return false;
    }

    if (year != NULL)
        *year = 2000 + bcd_to_decimal(data[6] & 0xFF);

    if (month != NULL)
        *month = bcd_to_decimal(data[5] & 0x1F);

    if (date != NULL)
        *date = bcd_to_decimal(data[4] & 0x3F);

    if (day != NULL)
        *day = bcd_to_decimal(data[3] & 0x07);

    if (hour != NULL)
        *hour = bcd_to_decimal(data[2] & 0x3F);

    if (minute != NULL)
        *minute = bcd_to_decimal(data[1] & 0x7F);

    if (second != NULL)
        *second = bcd_to_decimal(data[0] & 0x7F);

    return true;
}
