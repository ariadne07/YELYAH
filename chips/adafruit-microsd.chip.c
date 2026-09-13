#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SPI Commands
#define CMD0   0x40  // GO_IDLE_STATE
#define CMD8   0x48  // SEND_IF_COND
#define CMD17  0x51  // READ_SINGLE_BLOCK
#define CMD55  0x77  // APP_CMD
#define ACMD41 0x69  // SD_SEND_OP_COND

typedef enum {
  STATE_IDLE,
  STATE_RECEIVING_ARGS,
  STATE_RESPONSE,
  STATE_READ_BLOCK_DATA
} sd_state_t;

typedef struct {
  pin_t pin_cs;
  pin_t pin_sck;
  pin_t pin_mosi;
  pin_t pin_miso;
  spi_dev_t spi;

  sd_state_t state;
  uint8_t command_buf[6];
  uint8_t buf_index;
  uint8_t response_buf[10];
  uint8_t response_len;
  uint8_t response_idx;
  bool is_app_cmd;

  uint8_t dummy_block[512];
  uint16_t block_tx_idx;
} chip_state_t;

static void process_sd_command(chip_state_t *chip) {
  uint8_t cmd = chip->command_buf[0];
  chip->response_idx = 0;

  if (chip->is_app_cmd) {
    chip->is_app_cmd = false;
    if (cmd == ACMD41) {
      chip->response_buf[0] = 0x00; // Initialization successful
      chip->response_len = 1;
      chip->state = STATE_RESPONSE;
      return;
    }
  }

  switch (cmd) {
    case CMD0:
      chip->response_buf[0] = 0x01; // In Idle State (R1)
      chip->response_len = 1;
      chip->state = STATE_RESPONSE;
      break;

    case CMD8:
      chip->response_buf[0] = 0x01; // R1
      chip->response_buf[1] = 0x00;
      chip->response_buf[2] = 0x00;
      chip->response_buf[3] = 0x01;
      chip->response_buf[4] = chip->command_buf[4]; // Echo pattern
      chip->response_len = 5;
      chip->state = STATE_RESPONSE;
      break;

    case CMD55:
      chip->is_app_cmd = true;
      chip->response_buf[0] = 0x01;
      chip->response_len = 1;
      chip->state = STATE_RESPONSE;
      break;

    case CMD17:
      chip->response_buf[0] = 0x00; // Success
      chip->response_len = 1;
      chip->block_tx_idx = 0;
      chip->state = STATE_READ_BLOCK_DATA;
      break;

    default:
      chip->response_buf[0] = 0x04; // Illegal command
      chip->response_len = 1;
      chip->state = STATE_RESPONSE;
      break;
  }
}

static uint8_t transfer_byte(chip_state_t *chip, uint8_t byte_in) {
  uint8_t byte_out = 0xFF;

  switch (chip->state) {
    case STATE_IDLE:
      if ((byte_in & 0xC0) == 0x40) {
        chip->command_buf[0] = byte_in;
        chip->buf_index = 1;
        chip->state = STATE_RECEIVING_ARGS;
      }
      break;

    case STATE_RECEIVING_ARGS:
      chip->command_buf[chip->buf_index++] = byte_in;
      if (chip->buf_index >= 6) {
        process_sd_command(chip);
      }
      break;

    case STATE_RESPONSE:
      byte_out = chip->response_buf[chip->response_idx++];
      if (chip->response_idx >= chip->response_len) {
        chip->state = STATE_IDLE;
      }
      break;

    case STATE_READ_BLOCK_DATA:
      if (chip->response_idx < chip->response_len) {
        byte_out = chip->response_buf[chip->response_idx++];
      } else if (chip->block_tx_idx == 0) {
        byte_out = 0xFE; // Start block token
        chip->block_tx_idx++;
      } else if (chip->block_tx_idx <= 512) {
        byte_out = chip->dummy_block[chip->block_tx_idx - 1];
        chip->block_tx_idx++;
      } else if (chip->block_tx_idx <= 514) {
        byte_out = 0xFF; // CRC bytes
        chip->block_tx_idx++;
        if (chip->block_tx_idx > 514) {
          chip->state = STATE_IDLE;
        }
      }
      break;
  }

  return byte_out;
}

static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t *)user_data;

  if (pin_read(chip->pin_cs) != LOW) {
    memset(buffer, 0xFF, count);
    return;
  }

  for (uint32_t i = 0; i < count; i++) {
    buffer[i] = transfer_byte(chip, buffer[i]);
  }
}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)malloc(sizeof(chip_state_t));
  memset(chip, 0, sizeof(chip_state_t));

  chip->pin_cs   = pin_init("CS", INPUT_PULLUP);
  chip->pin_sck  = pin_init("SCK", INPUT);
  chip->pin_mosi = pin_init("MOSI", INPUT);
  chip->pin_miso = pin_init("MISO", OUTPUT);

  spi_config_t spi_config = {
    .sck = chip->pin_sck,
    .mosi = chip->pin_mosi,
    .miso = chip->pin_miso,
    .done = chip_spi_done,
    .user_data = chip,
    .mode = 0
  };

  chip->spi = spi_init(&spi_config);
  chip->state = STATE_IDLE;

  for (int i = 0; i < 512; i++) {
    chip->dummy_block[i] = (uint8_t)(i & 0xFF);
  }
}