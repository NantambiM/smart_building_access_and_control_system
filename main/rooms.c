
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

// EXTERNAL FUNCTIONS

void sd_logger_log_event(
    const char *event,
    const char *description
);

// PIN CONFIGURATION

// ROOM 1
#define PIR_ROOM1       GPIO_NUM_27
#define LIGHT_ROOM1     GPIO_NUM_2
#define LDR_ROOM1       ADC_CHANNEL_6  // GPIO34

// ROOM 2
#define PIR_ROOM2       GPIO_NUM_26
#define LIGHT_ROOM2     GPIO_NUM_4
#define LDR_ROOM2       ADC_CHANNEL_7  // GPIO35

// CONFIGURATION

#define DARK_THRESHOLD_ADC 2000

#define LIGHT_TIMEOUT_MS   10000
#define LDR_SAMPLE_MS      500

#define ROOM_QUEUE_LENGTH  16

static const char *TAG = "ROOM_MONITOR";

// DATA STRUCTURES

typedef enum {
    ROOM_1 = 1,
    ROOM_2 = 2
} room_id_t;

typedef enum {
    ROOM_EVENT_MOTION,
    ROOM_EVENT_TIMEOUT
} room_event_type_t;

typedef struct {
    room_id_t room;
    room_event_type_t type;
    int motion_state;
} room_event_t;

// FREERTOS HANDLES

static QueueHandle_t room_event_queue = NULL;

static TimerHandle_t room1_timeout_timer = NULL;
static TimerHandle_t room2_timeout_timer = NULL;

static adc_oneshot_unit_handle_t adc_handle;

// ROOM STATE VARIABLES

static bool room1_light_on = false;
static bool room2_light_on = false;

static bool room1_presence_window = false;
static bool room2_presence_window = false;

static bool room_monitoring_active = false;

// FORWARD DECLARATIONS

static void room_processing_task(void *arg);

static void update_room1_lighting(void);
static void update_room2_lighting(void);

// ISR: SHARED PIR INTERRUPT HANDLER

static void IRAM_ATTR pir_isr_handler(void *arg)
{
    room_id_t room = (room_id_t)(intptr_t)arg;

    gpio_num_t pir_pin =
        (room == ROOM_1) ? PIR_ROOM1 : PIR_ROOM2;

    int motion = gpio_get_level(pir_pin);

    room_event_t event = {
        .room = room,
        .type = ROOM_EVENT_MOTION,
        .motion_state = motion
    };

    BaseType_t high_task_awoken = pdFALSE;

    xQueueSendFromISR(
        room_event_queue,
        &event,
        &high_task_awoken
    );

    if (high_task_awoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

// TIMEOUT CALLBACKS

// Timer callbacks only send events.
// GPIO operations and SD logging happen in the processing task.

static void room1_timeout_callback(TimerHandle_t xTimer)
{
    room_event_t event = {
        .room = ROOM_1,
        .type = ROOM_EVENT_TIMEOUT,
        .motion_state = 0
    };

    xQueueSend(room_event_queue, &event, 0);
}

static void room2_timeout_callback(TimerHandle_t xTimer)
{
    room_event_t event = {
        .room = ROOM_2,
        .type = ROOM_EVENT_TIMEOUT,
        .motion_state = 0
    };

    xQueueSend(room_event_queue, &event, 0);
}

// ROOM 1 LIGHTING EVALUATION

static void update_room1_lighting(void)
{
    int ldr_reading = 0;

    esp_err_t result = adc_oneshot_read(
        adc_handle,
        LDR_ROOM1,
        &ldr_reading
    );

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Room 1 LDR reading failed: %s",
                 esp_err_to_name(result));
        return;
    }

    bool is_dark = (ldr_reading >= DARK_THRESHOLD_ADC);

    bool should_light_be_on =
        room1_presence_window && is_dark;

    if (should_light_be_on && !room1_light_on)
    {
        gpio_set_level(LIGHT_ROOM1, 1);
        room1_light_on = true;

        ESP_LOGI(TAG,
                 "[ROOM 1] Motion/presence and DARK. Light ON. LDR: %d",
                 ldr_reading);

        sd_logger_log_event(
            "LIGHT_ON",
            "Room 1 light switched ON: presence detected and insufficient ambient light"
        );
    }
    else if (!should_light_be_on && room1_light_on)
    {
        gpio_set_level(LIGHT_ROOM1, 0);
        room1_light_on = false;

        ESP_LOGI(TAG,
                 "[ROOM 1] Light OFF. LDR: %d",
                 ldr_reading);

        sd_logger_log_event(
            "LIGHT_OFF",
            is_dark
                ? "Room 1 light switched OFF after inactivity timeout"
                : "Room 1 light switched OFF because sufficient ambient light is available"
        );
    }

    ESP_LOGI(TAG,
             "[ROOM 1] LDR: %d | Environment: %s | Presence: %s | Light: %s",
             ldr_reading,
             is_dark ? "DARK" : "BRIGHT",
             room1_presence_window ? "YES" : "NO",
             room1_light_on ? "ON" : "OFF");
}

// ROOM 2 LIGHTING EVALUATION

static void update_room2_lighting(void)
{
    int ldr_reading = 0;

    esp_err_t result = adc_oneshot_read(
        adc_handle,
        LDR_ROOM2,
        &ldr_reading
    );

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Room 2 LDR reading failed: %s",
                 esp_err_to_name(result));
        return;
    }

    bool is_dark = (ldr_reading >= DARK_THRESHOLD_ADC);

    bool should_light_be_on =
        room2_presence_window && is_dark;

    if (should_light_be_on && !room2_light_on)
    {
        gpio_set_level(LIGHT_ROOM2, 1);
        room2_light_on = true;

        ESP_LOGI(TAG,
                 "[ROOM 2] Motion/presence and DARK. Light ON. LDR: %d",
                 ldr_reading);

        sd_logger_log_event(
            "LIGHT_ON",
            "Room 2 light switched ON: presence detected and insufficient ambient light"
        );
    }
    else if (!should_light_be_on && room2_light_on)
    {
        gpio_set_level(LIGHT_ROOM2, 0);
        room2_light_on = false;

        ESP_LOGI(TAG,
                 "[ROOM 2] Light OFF. LDR: %d",
                 ldr_reading);

        sd_logger_log_event(
            "LIGHT_OFF",
            is_dark
                ? "Room 2 light switched OFF after inactivity timeout"
                : "Room 2 light switched OFF because sufficient ambient light is available"
        );
    }

    ESP_LOGI(TAG,
             "[ROOM 2] LDR: %d | Environment: %s | Presence: %s | Light: %s",
             ldr_reading,
             is_dark ? "DARK" : "BRIGHT",
             room2_presence_window ? "YES" : "NO",
             room2_light_on ? "ON" : "OFF");
}

// INITIALIZE ROOM GPIO, ADC, INTERRUPTS & TASKS

void rooms_initialize(void)
{
    // 1. Initialise ADC1

    adc_oneshot_unit_init_cfg_t adc_init_config = {
        .unit_id = ADC_UNIT_1
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(
            &adc_init_config,
            &adc_handle
        )
    );

    // 2. Configure both LDR channels

    adc_oneshot_chan_cfg_t adc_channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc_handle,
            LDR_ROOM1,
            &adc_channel_config
        )
    );

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc_handle,
            LDR_ROOM2,
            &adc_channel_config
        )
    );

    // 3. Configure PIR sensors for interrupts on both edges

    gpio_config_t pir_config = {
        .pin_bit_mask =
            (1ULL << PIR_ROOM1) |
            (1ULL << PIR_ROOM2),

        .mode = GPIO_MODE_INPUT,

        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,

        .intr_type = GPIO_INTR_ANYEDGE
    };

    ESP_ERROR_CHECK(gpio_config(&pir_config));

    // 4. Configure room lights

    gpio_config_t light_config = {
        .pin_bit_mask =
            (1ULL << LIGHT_ROOM1) |
            (1ULL << LIGHT_ROOM2),

        .mode = GPIO_MODE_OUTPUT,

        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&light_config));

    gpio_set_level(LIGHT_ROOM1, 0);
    gpio_set_level(LIGHT_ROOM2, 0);

    // 5. Create event queue

    room_event_queue = xQueueCreate(
        ROOM_QUEUE_LENGTH,
        sizeof(room_event_t)
    );

    if (room_event_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create room event queue");
        return;
    }

    // 6. Create independent room timers

    room1_timeout_timer = xTimerCreate(
        "Room1Timer",
        pdMS_TO_TICKS(LIGHT_TIMEOUT_MS),
        pdFALSE,
        NULL,
        room1_timeout_callback
    );

    room2_timeout_timer = xTimerCreate(
        "Room2Timer",
        pdMS_TO_TICKS(LIGHT_TIMEOUT_MS),
        pdFALSE,
        NULL,
        room2_timeout_callback
    );

    if (room1_timeout_timer == NULL ||
        room2_timeout_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create room timers");
        return;
    }

    // 7. Install GPIO ISR service.
    // It may already have been installed by another module.

    esp_err_t isr_result = gpio_install_isr_service(0);

    if (isr_result != ESP_OK &&
        isr_result != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(isr_result);
    }

    // 8. Register individual PIR interrupt handlers

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            PIR_ROOM1,
            pir_isr_handler,
            (void *)(intptr_t)ROOM_1
        )
    );

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            PIR_ROOM2,
            pir_isr_handler,
            (void *)(intptr_t)ROOM_2
        )
    );

    // 9. Create background processing task

    BaseType_t task_result = xTaskCreate(
        room_processing_task,
        "room_proc_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (task_result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create room processing task");
        return;
    }

    room1_light_on = false;
    room2_light_on = false;

    room1_presence_window = false;
    room2_presence_window = false;

    room_monitoring_active = true;

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "INTERRUPT-DRIVEN ROOM SYSTEM READY");
    ESP_LOGI(TAG, "====================================");
}

// BACKGROUND ROOM PROCESSING TASK

static void room_processing_task(void *arg)
{
    room_event_t event;

    while (1)
    {
        // Wait for an interrupt event.
        // If no event arrives, wake up every 500 ms to
        // sample the LDRs of rooms with recent activity.

        if (xQueueReceive(
                room_event_queue,
                &event,
                pdMS_TO_TICKS(LDR_SAMPLE_MS)
            ) == pdPASS)
        {
            if (event.room == ROOM_1)
            {
                if (event.type == ROOM_EVENT_MOTION)
                {
                    if (event.motion_state == 1)
                    {
                        // Motion detected

                        room1_presence_window = true;

                        xTimerStop(room1_timeout_timer, 0);

                        ESP_LOGI(TAG, "[ROOM 1] PIR HIGH: Motion detected");

                        update_room1_lighting();
                    }
                    else
                    {
                        // Motion cleared: begin inactivity countdown

                        ESP_LOGI(TAG,
                                 "[ROOM 1] PIR LOW: Starting 10-second timeout");

                        xTimerReset(room1_timeout_timer, 0);
                    }
                }
                else if (event.type == ROOM_EVENT_TIMEOUT)
                {
                    // Check the actual PIR state before switching off.

                    if (gpio_get_level(PIR_ROOM1) == 0)
                    {
                        room1_presence_window = false;

                        ESP_LOGI(TAG,
                                 "[ROOM 1] Inactivity timeout expired");

                        update_room1_lighting();
                    }
                    else
                    {
                        // Motion has resumed. Keep the room active.

                        room1_presence_window = true;

                        xTimerStop(room1_timeout_timer, 0);

                        update_room1_lighting();
                    }
                }
            }

            else if (event.room == ROOM_2)
            {
                if (event.type == ROOM_EVENT_MOTION)
                {
                    if (event.motion_state == 1)
                    {
                        // Motion detected

                        room2_presence_window = true;

                        xTimerStop(room2_timeout_timer, 0);

                        ESP_LOGI(TAG, "[ROOM 2] PIR HIGH: Motion detected");

                        update_room2_lighting();
                    }
                    else
                    {
                        // Motion cleared: begin inactivity countdown

                        ESP_LOGI(TAG,
                                 "[ROOM 2] PIR LOW: Starting 10-second timeout");

                        xTimerReset(room2_timeout_timer, 0);
                    }
                }
                else if (event.type == ROOM_EVENT_TIMEOUT)
                {
                    // Check the actual PIR state before switching off.

                    if (gpio_get_level(PIR_ROOM2) == 0)
                    {
                        room2_presence_window = false;

                        ESP_LOGI(TAG,
                                 "[ROOM 2] Inactivity timeout expired");

                        update_room2_lighting();
                    }
                    else
                    {
                        // Motion has resumed. Keep the room active.

                        room2_presence_window = true;

                        xTimerStop(room2_timeout_timer, 0);

                        update_room2_lighting();
                    }
                }
            }
        }

        // Periodically evaluate ambient light only for rooms
        // that have recently detected motion.

        if (room1_presence_window)
        {
            update_room1_lighting();
        }

        if (room2_presence_window)
        {
            update_room2_lighting();
        }
    }
}

// COMPATIBILITY FUNCTION

// The room-processing task now handles room monitoring.
// Keep this function if main.c still calls it.

void manage_room_monitoring(void)
{
    // No polling required.
}

// ROOM STATUS GETTERS

bool rooms_is_monitoring_active(void)
{
    return room_monitoring_active;
}

bool rooms_is_room1_light_on(void)
{
    return room1_light_on;
}

bool rooms_is_room2_light_on(void)
{
    return room2_light_on;
}
