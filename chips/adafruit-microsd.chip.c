#include "wokwi-api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define CMD0    0
#define CMD8    8
#define CMD9    9
#define CMD10   10
#define CMD12   12
#define CMD13   13
#define CMD16   16
#define CMD17   17
#define CMD24   24
#define CMD55   55
#define CMD58   58
#define CMD59   59
#define ACMD41  41

#define SD_R1_IDLE_STATE       0x01
#define SD_R1_ILLEGAL_COMMAND  0x04

#define SD_TOKEN_START_BLOCK   0xFE

#define COMMAND_LENGTH         6
#define RESPONSE_BUFFER_SIZE   600

typedef struct {
    pin_t pin_cs;
    pin_t pin_clk;
    pin_t pin_mosi;
    pin_t pin_miso;

    spi_dev_t spi;

    uint8_t command_buf[COMMAND_LENGTH];
    uint8_t command_index;

    uint8_t current_command;
    uint32_t current_arg;

    bool app_cmd_pending;
    bool idle;
    bool initialized;
    bool crc_enabled;

    uint8_t response_buf[RESPONSE_BUFFER_SIZE];
    uint32_t response_len;
    uint32_t response_index;

    uint8_t block_data[512];
    uint32_t block_index;
    bool sending_block;
} chip_state_t;


/* -------------------------------------------------------------------------- */
/* Response helpers                                                           */
/* -------------------------------------------------------------------------- */

static void response_clear(chip_state_t *chip)
{
    chip->response_len = 0;
    chip->response_index = 0;
}


static void response_push(chip_state_t *chip, uint8_t value)
{
    if (chip->response_len < RESPONSE_BUFFER_SIZE) {
        chip->response_buf[chip->response_len] = value;
        chip->response_len++;
    }
}


static void response_r1(chip_state_t *chip, uint8_t r1)
{
    response_clear(chip);
    response_push(chip, r1);
}


static void response_r7(chip_state_t *chip, uint8_t r1, uint32_t value)
{
    response_clear(chip);

    response_push(chip, r1);
    response_push(chip, (uint8_t)((value >> 24) & 0xFF));
    response_push(chip, (uint8_t)((value >> 16) & 0xFF));
    response_push(chip, (uint8_t)((value >> 8) & 0xFF));
    response_push(chip, (uint8_t)(value & 0xFF));
}


static void response_r3(chip_state_t *chip, uint8_t r1, uint32_t value)
{
    response_clear(chip);

    response_push(chip, r1);
    response_push(chip, (uint8_t)((value >> 24) & 0xFF));
    response_push(chip, (uint8_t)((value >> 16) & 0xFF));
    response_push(chip, (uint8_t)((value >> 8) & 0xFF));
    response_push(chip, (uint8_t)(value & 0xFF));
}


/* -------------------------------------------------------------------------- */
/* Card helpers                                                               */
/* -------------------------------------------------------------------------- */

static uint8_t current_r1(chip_state_t *chip)
{
    if (chip->idle) {
        return SD_R1_IDLE_STATE;
    }

    return 0x00;
}


static void reset_command_parser(chip_state_t *chip)
{
    chip->command_index = 0;

    memset(
        chip->command_buf,
        0,
        sizeof(chip->command_buf)
    );
}


static void prepare_test_block(chip_state_t *chip)
{
    for (uint32_t i = 0; i < 512; i++) {
        chip->block_data[i] = (uint8_t)(i & 0xFF);
    }

    chip->block_index = 0;
    chip->sending_block = true;
}


/* -------------------------------------------------------------------------- */
/* Command processor                                                          */
/* -------------------------------------------------------------------------- */

static void process_command(chip_state_t *chip)
{
    uint8_t raw_command = chip->command_buf[0];
    uint8_t command = raw_command & 0x3F;

    uint32_t arg =
        ((uint32_t)chip->command_buf[1] << 24) |
        ((uint32_t)chip->command_buf[2] << 16) |
        ((uint32_t)chip->command_buf[3] << 8)  |
        ((uint32_t)chip->command_buf[4]);

    uint8_t crc = chip->command_buf[5];

    chip->current_command = command;
    chip->current_arg = arg;

    printf(
        "[chip-adafruit-microsd] SD CHIP: CMD%u arg=0x%08X crc=0x%02X\n",
        (unsigned)command,
        (unsigned)arg,
        crc
    );

    response_clear(chip);
    chip->sending_block = false;


    /*
     * CMD55 means the next command is an application command.
     */
    if (command == CMD55) {
        chip->app_cmd_pending = true;

        printf(
            "[chip-adafruit-microsd] SD CHIP: CMD55\n"
        );

        response_r1(
            chip,
            current_r1(chip)
        );

        return;
    }


    /*
     * ACMD41 initializes the card.
     */
    if (command == ACMD41 && chip->app_cmd_pending) {
        chip->app_cmd_pending = false;

        chip->idle = false;
        chip->initialized = true;

        printf(
            "[chip-adafruit-microsd] SD CHIP: ACMD41 -> READY\n"
        );

        response_r1(
            chip,
            0x00
        );

        return;
    }


    chip->app_cmd_pending = false;


    switch (command) {

        case CMD0:
            /*
             * GO_IDLE_STATE
             */
            chip->idle = true;
            chip->initialized = false;

            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD0 -> IDLE\n"
            );

            response_r1(
                chip,
                SD_R1_IDLE_STATE
            );

            break;


        case CMD8: {
            /*
             * SEND_IF_COND
             */
            uint8_t check_pattern =
                (uint8_t)(arg & 0xFF);

            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD8 echo=0x%02X\n",
                check_pattern
            );

            response_r7(
                chip,
                SD_R1_IDLE_STATE,
                0x00000100UL | check_pattern
            );

            break;
        }


        case CMD58:
            /*
             * READ_OCR
             *
             * CCS = 1
             * Voltage range = 2.7V - 3.6V
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD58 -> OCR\n"
            );

            response_r3(
                chip,
                current_r1(chip),
                0x40FF8000UL
            );

            break;


        case CMD59:
            /*
             * CRC_ON_OFF
             */
            chip->crc_enabled =
                (arg & 1U) != 0;

            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD59 CRC %s\n",
                chip->crc_enabled ? "ON" : "OFF"
            );

            response_r1(
                chip,
                current_r1(chip)
            );

            break;


        case CMD16:
            /*
             * SET_BLOCKLEN
             */
            if (arg == 512) {

                printf(
                    "[chip-adafruit-microsd] SD CHIP: CMD16 block length 512\n"
                );

                response_r1(
                    chip,
                    0x00
                );

            } else {

                printf(
                    "[chip-adafruit-microsd] SD CHIP: CMD16 unsupported length %u\n",
                    (unsigned)arg
                );

                response_r1(
                    chip,
                    SD_R1_ILLEGAL_COMMAND
                );
            }

            break;


        case CMD17:
            /*
             * READ_SINGLE_BLOCK
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD17 block=%u\n",
                (unsigned)arg
            );

            prepare_test_block(chip);

            response_r1(
                chip,
                0x00
            );

            break;


        case CMD24:
            /*
             * WRITE_SINGLE_BLOCK
             *
             * Acknowledge for now. Actual storage comes later.
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD24 block=%u\n",
                (unsigned)arg
            );

            response_r1(
                chip,
                0x00
            );

            break;


        case CMD9:
            /*
             * SEND_CSD
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD9 -> CSD\n"
            );

            response_r1(
                chip,
                0x00
            );

            response_push(chip, 0xFE);

            response_push(chip, 0x40);
            response_push(chip, 0x0E);
            response_push(chip, 0x00);
            response_push(chip, 0x32);
            response_push(chip, 0x5B);
            response_push(chip, 0x59);
            response_push(chip, 0x00);
            response_push(chip, 0x00);
            response_push(chip, 0x00);
            response_push(chip, 0x00);
            response_push(chip, 0x7F);
            response_push(chip, 0x80);
            response_push(chip, 0x0A);
            response_push(chip, 0x40);
            response_push(chip, 0x00);
            response_push(chip, 0xAF);

            response_push(chip, 0xFF);
            response_push(chip, 0xFF);

            break;


        case CMD10:
            /*
             * SEND_CID
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD10 -> CID\n"
            );

            response_r1(
                chip,
                0x00
            );

            response_push(chip, 0xFE);

            response_push(chip, 0x03);
            response_push(chip, 'Y');
            response_push(chip, 'E');
            response_push(chip, 'L');
            response_push(chip, 'Y');
            response_push(chip, 'A');
            response_push(chip, 'H');
            response_push(chip, 0x20);
            response_push(chip, 0x26);
            response_push(chip, 0x09);
            response_push(chip, 0x13);
            response_push(chip, 0x12);
            response_push(chip, 0x34);
            response_push(chip, 0x56);
            response_push(chip, 0x78);
            response_push(chip, 0x01);

            response_push(chip, 0xFF);
            response_push(chip, 0xFF);

            break;


        case CMD12:
            /*
             * STOP_TRANSMISSION
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD12\n"
            );

            response_r1(
                chip,
                0x00
            );

            break;


        case CMD13:
            /*
             * SEND_STATUS
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD13\n"
            );

            response_r1(
                chip,
                0x00
            );

            response_push(
                chip,
                0x00
            );

            break;


        case 5:
        case 52:
            /*
             * SDIO commands are not supported.
             */
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD%u -> illegal\n",
                (unsigned)command
            );

            response_r1(
                chip,
                SD_R1_ILLEGAL_COMMAND
            );

            break;


        default:
            printf(
                "[chip-adafruit-microsd] SD CHIP: CMD%u -> unsupported\n",
                (unsigned)command
            );

            response_r1(
                chip,
                SD_R1_ILLEGAL_COMMAND
            );

            break;
    }
}


/* -------------------------------------------------------------------------- */
/* Generate the next byte sent to ESP32                                      */
/* -------------------------------------------------------------------------- */

static uint8_t get_next_tx_byte(chip_state_t *chip)
{
    /*
     * Send response bytes first.
     */
    if (chip->response_index < chip->response_len) {

        uint8_t value =
            chip->response_buf[chip->response_index];

        chip->response_index++;

        return value;
    }


    /*
     * CMD17 data block.
     */
    if (chip->sending_block) {

        /*
         * Data start token.
         */
        if (chip->block_index == 0) {
            chip->block_index = 1;
            return SD_TOKEN_START_BLOCK;
        }


        /*
         * 512 bytes of data.
         */
        if (chip->block_index <= 512) {

            uint8_t value =
                chip->block_data[chip->block_index - 1];

            chip->block_index++;

            return value;
        }


        /*
         * Fake CRC16 byte 1.
         */
        if (chip->block_index == 513) {

            chip->block_index++;

            return 0xFF;
        }


        /*
         * Fake CRC16 byte 2.
         */
        if (chip->block_index == 514) {

            chip->block_index++;

            chip->sending_block = false;

            return 0xFF;
        }
    }


    /*
     * SD cards send 0xFF when idle.
     */
    return 0xFF;
}


/* -------------------------------------------------------------------------- */
/* SPI callback                                                               */
/* -------------------------------------------------------------------------- */

static void chip_spi_done(
    void *user_data,
    uint8_t *buffer,
    uint32_t count
)
{
    chip_state_t *chip =
        (chip_state_t *)user_data;

    if (count == 0) {
        return;
    }


    /*
     * Byte received from ESP32.
     */
    uint8_t rx = buffer[0];

    printf(
        "[chip-adafruit-microsd] SD CHIP: RX 0x%02X\n",
        rx
    );


    /*
     * Build a six-byte SD command.
     */
    if (chip->command_index < COMMAND_LENGTH) {

        chip->command_buf[chip->command_index] = rx;
        chip->command_index++;

        if (chip->command_index == 1) {

            printf(
                "[chip-adafruit-microsd] SD CHIP: COMMAND BYTE 0x%02X\n",
                rx
            );
        }


        if (chip->command_index == COMMAND_LENGTH) {

            process_command(chip);
        }
    }


    /*
     * CS LOW means the ESP32 still has the card selected.
     */
    if (pin_read(chip->pin_cs) == LOW) {

        uint8_t tx =
            get_next_tx_byte(chip);

        buffer[0] = tx;


        /*
         * Normal command response finished:
         * prepare to receive the next command.
         */
        if (chip->response_len > 0 &&
            chip->response_index >= chip->response_len &&
            !chip->sending_block) {

            response_clear(chip);
            reset_command_parser(chip);
        }


        /*
         * Start the next SPI byte.
         */
        spi_start(
            chip->spi,
            buffer,
            1
        );
    }
}


/* -------------------------------------------------------------------------- */
/* Chip-select callback                                                       */
/* -------------------------------------------------------------------------- */

static void chip_cs_changed(
    void *user_data,
    pin_t pin,
    uint32_t value
)
{
    chip_state_t *chip =
        (chip_state_t *)user_data;

    (void)pin;


    if (value == LOW) {

        printf(
            "[chip-adafruit-microsd] SD CHIP: CS LOW\n"
        );


        /*
         * New SPI transaction.
         */
        reset_command_parser(chip);
        response_clear(chip);

        chip->current_command = 0xFF;
        chip->current_arg = 0;

        chip->sending_block = false;


        /*
         * Initial MISO value.
         */
        uint8_t tx = 0xFF;

        spi_start(
            chip->spi,
            &tx,
            1
        );

    } else {

        printf(
            "[chip-adafruit-microsd] SD CHIP: CS HIGH\n"
        );


        spi_stop(chip->spi);


        /*
         * End transaction.
         */
        reset_command_parser(chip);
        response_clear(chip);

        chip->current_command = 0xFF;
        chip->current_arg = 0;

        chip->sending_block = false;
    }
}


/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

void chip_init(void)
{
    chip_state_t *chip =
        calloc(
            1,
            sizeof(chip_state_t)
        );


    if (chip == NULL) {

        printf(
            "[chip-adafruit-microsd] SD CHIP: allocation failed\n"
        );

        return;
    }


    /*
     * Custom chip pin names must match the .chip.json:
     *
     * CLK
     * SO
     * SI
     * CS
     */
    chip->pin_cs =
        pin_init("CS", INPUT);

    chip->pin_clk =
        pin_init("CLK", INPUT);

    chip->pin_mosi =
        pin_init("SI", INPUT);

    chip->pin_miso =
        pin_init("SO", OUTPUT);


    chip->idle = true;
    chip->initialized = false;
    chip->crc_enabled = false;
    chip->app_cmd_pending = false;

    chip->current_command = 0xFF;
    chip->current_arg = 0;

    chip->sending_block = false;


    /*
     * SPI configuration.
     */
    const spi_config_t spi_config = {
        .sck = chip->pin_clk,
        .mosi = chip->pin_mosi,
        .miso = chip->pin_miso,
        .mode = 0,
        .done = chip_spi_done,
        .user_data = chip
    };


    chip->spi =
        spi_init(&spi_config);


    /*
     * Wokwi pin-watch API.
     */
    const pin_watch_config_t cs_watch_config = {
        .edge = BOTH,
        .pin_change = chip_cs_changed,
        .user_data = chip
    };


    pin_watch(
        chip->pin_cs,
        &cs_watch_config
    );


    printf(
        "[chip-adafruit-microsd] SD CHIP: initialized\n"
    );
}