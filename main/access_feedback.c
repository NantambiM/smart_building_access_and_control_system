#include <stdio.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

// ============================================
// ACCESS FEEDBACK PIN CONFIGURATION
// ============================================

#define RED_LED_PIN       GPIO_NUM_16
#define GREEN_LED_PIN     GPIO_NUM_25
#define BUZZER_PIN        GPIO_NUM_15

#define BUZZER_FREQUENCY  2000
#define BUZZER_RESOLUTION LEDC_TIMER_10_BIT

// ============================================
// BUZZER CONTROL
// ============================================

static void buzzer_on(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void buzzer_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ============================================
// INITIALIZE FEEDBACK SYSTEM
// ============================================

void access_feedback_initialize(void)
{
    gpio_config_t led_config = {
        .pin_bit_mask =
            (1ULL << RED_LED_PIN) |
            (1ULL << GREEN_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&led_config));

    gpio_set_level(RED_LED_PIN, 0);
    gpio_set_level(GREEN_LED_PIN, 0);

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BUZZER_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BUZZER_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = BUZZER_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };

    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    printf("[FEEDBACK] LEDs and buzzer initialized.\n");
}

// ============================================
// SUCCESSFUL RFID SCAN
// ============================================

void access_feedback_success(void)
{
    gpio_set_level(RED_LED_PIN, 0);
    gpio_set_level(GREEN_LED_PIN, 1);

    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(150));

    buzzer_off();
    vTaskDelay(pdMS_TO_TICKS(150));

    gpio_set_level(GREEN_LED_PIN, 0);

    printf("[FEEDBACK] ACCESS GRANTED.\n");
}

// ============================================
// UNSUCCESSFUL RFID SCAN
// ============================================

void access_feedback_failure(void)
{
    gpio_set_level(GREEN_LED_PIN, 0);
    gpio_set_level(RED_LED_PIN, 1);

    // First beep
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(150));

    buzzer_off();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Second beep
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(150));

    buzzer_off();
    vTaskDelay(pdMS_TO_TICKS(150));

    gpio_set_level(RED_LED_PIN, 0);

    printf("[FEEDBACK] ACCESS DENIED.\n");
}