
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_timer.h"

// =====================================================
// READER IDENTIFIERS
// =====================================================

#define RFID_ENTRY 0
#define RFID_EXIT  1
#define RFID_READER_COUNT 2

// =====================================================
// SHARED SPI PINS
// =====================================================

#define RFID_SCK  GPIO_NUM_18
#define RFID_MOSI GPIO_NUM_23
#define RFID_MISO GPIO_NUM_19

// =====================================================
// MFRC522 REGISTERS
// =====================================================

#define CommandReg       0x01
#define ComIrqReg        0x04
#define ErrorReg         0x06
#define FIFODataReg      0x09
#define FIFOLevelReg     0x0A
#define BitFramingReg    0x0D
#define TxControlReg     0x14
#define TxASKReg         0x15
#define ModeReg          0x11
#define TModeReg         0x2A
#define TPrescalerReg    0x2B
#define TReloadRegH      0x2C
#define TReloadRegL      0x2D
#define VersionReg       0x37

// =====================================================
// MFRC522 COMMANDS
// =====================================================

#define PCD_Idle         0x00
#define PCD_Transceive   0x0C
#define PCD_SoftReset    0x0F

#define PICC_REQA        0x26
#define PICC_ANTICOLL    0x93

// =====================================================
// RFID READER CONFIGURATION
// =====================================================

typedef struct
{
    gpio_num_t ss_pin;
    gpio_num_t rst_pin;
    spi_device_handle_t spi;

} mfrc522_reader_t;

static mfrc522_reader_t readers[RFID_READER_COUNT] =
{
    [RFID_ENTRY] = {
        .ss_pin = GPIO_NUM_5,
        .rst_pin = GPIO_NUM_22
    },

    [RFID_EXIT] = {
        .ss_pin = GPIO_NUM_17,
        .rst_pin = GPIO_NUM_21
    }
};

// =====================================================
// INITIALIZATION FLAGS
// =====================================================

static bool spi_bus_ready = false;
static bool readers_ready = false;

// =====================================================
// READER VALIDATION
// =====================================================

static mfrc522_reader_t *get_reader(int reader_id)
{
    if (reader_id < 0 || reader_id >= RFID_READER_COUNT)
    {
        return NULL;
    }

    return &readers[reader_id];
}

// =====================================================
// SPI REGISTER WRITE
// =====================================================

static void rfid_write_register(
    mfrc522_reader_t *reader,
    uint8_t reg,
    uint8_t value)
{
    uint8_t tx_data[2];

    tx_data[0] = (reg << 1) & 0x7E;
    tx_data[1] = value;

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx_data
    };

    gpio_set_level(reader->ss_pin, 0);

    ESP_ERROR_CHECK(
        spi_device_polling_transmit(
            reader->spi,
            &transaction
        )
    );

    gpio_set_level(reader->ss_pin, 1);
}

// =====================================================
// SPI REGISTER READ
// =====================================================

static uint8_t rfid_read_register(
    mfrc522_reader_t *reader,
    uint8_t reg)
{
    uint8_t tx_data[2] = {
        ((reg << 1) & 0x7E) | 0x80,
        0x00
    };

    uint8_t rx_data[2] = {0};

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    gpio_set_level(reader->ss_pin, 0);

    ESP_ERROR_CHECK(
        spi_device_polling_transmit(
            reader->spi,
            &transaction
        )
    );

    gpio_set_level(reader->ss_pin, 1);

    return rx_data[1];
}

// =====================================================
// FIFO BURST READ
// =====================================================

static void rfid_read_fifo(
    mfrc522_reader_t *reader,
    uint8_t *data,
    uint8_t length)
{
    if (length == 0 || length > 64)
    {
        return;
    }

    uint8_t tx_data[65] = {0};
    uint8_t rx_data[65] = {0};

    tx_data[0] =
        ((FIFODataReg << 1) & 0x7E) | 0x80;

    spi_transaction_t transaction = {
        .length = (length + 1) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    gpio_set_level(reader->ss_pin, 0);

    ESP_ERROR_CHECK(
        spi_device_polling_transmit(
            reader->spi,
            &transaction
        )
    );

    gpio_set_level(reader->ss_pin, 1);

    for (uint8_t i = 0; i < length; i++)
    {
        data[i] = rx_data[i + 1];
    }
}

// =====================================================
// RFID TRANSCEIVE
// =====================================================

static bool rfid_transceive(
    mfrc522_reader_t *reader,
    const uint8_t *send_data,
    uint8_t send_length,
    uint8_t tx_last_bits,
    uint8_t *receive_data,
    uint8_t *receive_length)
{
    rfid_write_register(reader, CommandReg, PCD_Idle);
    rfid_write_register(reader, ComIrqReg, 0x7F);
    rfid_write_register(reader, FIFOLevelReg, 0x80);

    for (uint8_t i = 0; i < send_length; i++)
    {
        rfid_write_register(
            reader,
            FIFODataReg,
            send_data[i]
        );
    }

    rfid_write_register(
        reader,
        BitFramingReg,
        tx_last_bits
    );

    rfid_write_register(
        reader,
        CommandReg,
        PCD_Transceive
    );

    rfid_write_register(
        reader,
        BitFramingReg,
        tx_last_bits | 0x80
    );

    int64_t start_time = esp_timer_get_time();

    uint8_t irq = 0;

    while ((esp_timer_get_time() - start_time) < 50000)
    {
        irq = rfid_read_register(reader, ComIrqReg);

        if (irq & 0x01)
        {
            rfid_write_register(
                reader,
                BitFramingReg,
                tx_last_bits
            );

            return false;
        }

        if (irq & 0x30)
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    rfid_write_register(
        reader,
        BitFramingReg,
        tx_last_bits
    );

    if (!(irq & 0x30))
    {
        return false;
    }

    uint8_t error =
        rfid_read_register(reader, ErrorReg);

    if (error & 0x1B)
    {
        return false;
    }

    uint8_t count =
        rfid_read_register(reader, FIFOLevelReg);

    if (count == 0 || count > 64)
    {
        return false;
    }

    rfid_read_fifo(reader, receive_data, count);

    *receive_length = count;

    return true;
}

// =====================================================
// INITIALIZE ONE RFID READER
// =====================================================

static void initialize_one_reader(
    mfrc522_reader_t *reader,
    const char *name)
{
    printf("\nInitializing %s...\n", name);

    gpio_set_level(reader->rst_pin, 1);
    gpio_set_level(reader->ss_pin, 1);

    gpio_set_level(reader->rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(reader->rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    rfid_write_register(reader, CommandReg, PCD_SoftReset);
    vTaskDelay(pdMS_TO_TICKS(50));

    rfid_write_register(reader, TModeReg, 0x8D);
    rfid_write_register(reader, TPrescalerReg, 0x3E);
    rfid_write_register(reader, TReloadRegL, 30);
    rfid_write_register(reader, TReloadRegH, 0);

    rfid_write_register(reader, TxASKReg, 0x40);
    rfid_write_register(reader, ModeReg, 0x3D);

    uint8_t tx_control =
        rfid_read_register(reader, TxControlReg);

    if ((tx_control & 0x03) != 0x03)
    {
        rfid_write_register(
            reader,
            TxControlReg,
            tx_control | 0x03
        );
    }

    uint8_t version =
        rfid_read_register(reader, VersionReg);

    printf("%s MFRC522 Version: 0x%02X\n",
           name, version);

    if (version == 0x00 || version == 0xFF)
    {
        printf("WARNING: %s did not respond correctly.\n", name);
    }
    else
    {
        printf("%s initialized successfully.\n", name);
    }
}

// =====================================================
// STEP 1: INITIALIZE SHARED SPI BUS
// =====================================================

void mfrc522_spi_initialize(void)
{
    if (spi_bus_ready)
    {
        printf("[RFID] SPI bus already initialized.\n");
        return;
    }

    printf("\nConfiguring RFID GPIO pins...\n");

    uint64_t pin_mask =
        (1ULL << readers[RFID_ENTRY].ss_pin) |
        (1ULL << readers[RFID_ENTRY].rst_pin) |
        (1ULL << readers[RFID_EXIT].ss_pin) |
        (1ULL << readers[RFID_EXIT].rst_pin);

    gpio_config_t pin_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&pin_config));

    // Keep both RFID readers inactive.
    gpio_set_level(readers[RFID_ENTRY].ss_pin, 1);
    gpio_set_level(readers[RFID_EXIT].ss_pin, 1);

    gpio_set_level(readers[RFID_ENTRY].rst_pin, 1);
    gpio_set_level(readers[RFID_EXIT].rst_pin, 1);

    printf("[RFID] Initializing shared SPI bus...\n");

    spi_bus_config_t bus_config = {
        .mosi_io_num = RFID_MOSI,
        .miso_io_num = RFID_MISO,
        .sclk_io_num = RFID_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        // Large enough for SD card sector transfers.
        .max_transfer_sz = 4096
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            SPI2_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
        )
    );

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1
    };

    ESP_ERROR_CHECK(
        spi_bus_add_device(
            SPI2_HOST,
            &device_config,
            &readers[RFID_ENTRY].spi
        )
    );

    ESP_ERROR_CHECK(
        spi_bus_add_device(
            SPI2_HOST,
            &device_config,
            &readers[RFID_EXIT].spi
        )
    );

    spi_bus_ready = true;

    printf("[RFID] Shared SPI bus initialized.\n");
    printf("[RFID] Both reader devices registered.\n");
}

// =====================================================
// STEP 2: INITIALIZE RFID CHIPS
// =====================================================

void mfrc522_readers_initialize(void)
{
    if (!spi_bus_ready)
    {
        printf("[RFID] ERROR: SPI bus has not been initialized.\n");
        return;
    }

    if (readers_ready)
    {
        printf("[RFID] Readers already initialized.\n");
        return;
    }

    printf("\nInitializing RFID chips...\n");

    initialize_one_reader(
        &readers[RFID_ENTRY],
        "ENTRY READER"
    );

    initialize_one_reader(
        &readers[RFID_EXIT],
        "EXIT READER"
    );

    readers_ready = true;

    printf("\nBoth RFID readers initialized.\n");
}

// =====================================================
// CARD REQUEST
// =====================================================

bool mfrc522_request(int reader_id)
{
    if (!readers_ready)
    {
        return false;
    }

    mfrc522_reader_t *reader = get_reader(reader_id);

    if (reader == NULL)
    {
        return false;
    }

    uint8_t request_command = PICC_REQA;

    uint8_t response[10] = {0};
    uint8_t response_length = 0;

    bool result = rfid_transceive(
        reader,
        &request_command,
        1,
        7,
        response,
        &response_length
    );

    return result && response_length == 2;
}

// =====================================================
// READ CARD UID
// =====================================================

bool mfrc522_read_uid(int reader_id, uint8_t *uid)
{
    if (!readers_ready)
    {
        return false;
    }

    mfrc522_reader_t *reader = get_reader(reader_id);

    if (reader == NULL || uid == NULL)
    {
        return false;
    }

    uint8_t anticollision_command[2] = {
        PICC_ANTICOLL,
        0x20
    };

    uint8_t response[10] = {0};
    uint8_t response_length = 0;

    bool result = rfid_transceive(
        reader,
        anticollision_command,
        2,
        0,
        response,
        &response_length
    );

    if (!result || response_length != 5)
    {
        printf("UID reading failed.\n");
        return false;
    }

    uint8_t checksum =
        response[0] ^
        response[1] ^
        response[2] ^
        response[3];

    if (checksum != response[4])
    {
        printf("UID checksum validation failed.\n");
        return false;
    }

    for (int i = 0; i < 4; i++)
    {
        uid[i] = response[i];
    }

    return true;
}
