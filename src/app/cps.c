/*
 * cps.c - CPS programming protocol handler for RT-950 Pro
 *
 * V0.27 firmware CPS-related addresses:
 *   UART4 init:         fw 0x08022762  (115200 baud, PC10/PC11)
 *   CPS state struct:   RAM 0x2000A3B4
 *   Model string:       fw 0x080003E0  "RT-950      " (12 chars)
 *   Model string (dup):  fw 0x08000BE0  same
 *   CPS mode check:     fw 0x0800C10E  cmp r0, 0xA5 (mode flag at offset 0x4A)
 *
 * Frame: [0xA5][0xFF][0xFF][0xFF][Cmd][Len][Data...][CRC_H][CRC_L]
 * CRC: CRC-CCITT polynomial 0x1021, initial value 0x0000.
 *
 * NOTE: The only CRC polynomial found in the V0.27 binary is 0x8408
 * (reflected CRC-CCITT, init 0xFFFF) at fw 0x08024E52. This function
 * appears to be for APRS AX.25 FCS, not CPS framing. The CPS protocol
 * CRC implementation needs further verification against the PC CPS
 * software to confirm which variant is used.
 */

#include "app/cps.h"
#include "drivers/uart.h"
#include "drivers/spi.h"
#include "drivers/bk4829.h"
#include "drivers/flash_crc.h"
#include "at32f403a.h"

#include <string.h>

/* Model identification -------------------------------------------- */
/* V0.27 fw 0x080003E0: "RT-950" + 6 spaces = 12 printable chars */

/* Exactly 12 bytes on the wire, space-padded and deliberately NOT
 * NUL-terminated: the CPS reads a fixed-width field, not a C string. GCC 15
 * added -Wunterminated-string-initialization, which flags this by default;
 * nonstring records that the missing terminator is intentional. The value is
 * only ever passed to a sized write. */
static const char model_string[12] __attribute__((nonstring)) = "RT-950      ";

/* State ----------------------------------------------------------- */

static cps_state_t cps_state;
static uint8_t     handshake_count;
static uint8_t     rx_buf[CPS_PACKET_SIZE + 16];  /* frame buffer */

static uint16_t    rx_pos;

/* CRC-CCITT ------------------------------------------------------- */

uint16_t cps_crc_ccitt(const uint8_t *data, uint16_t offset, uint16_t length)
{
    return crc16_ccitt(data + offset, length);
}

/* Frame TX helper ------------------------------------------------- */

static void cps_send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[CPS_PACKET_SIZE + 16];
    frame[0] = CPS_FRAME_HEADER;
    frame[1] = CPS_FRAME_PAD;
    frame[2] = CPS_FRAME_PAD;
    frame[3] = CPS_FRAME_PAD;
    frame[4] = cmd;
    frame[5] = len;
    if (len > 0 && payload)
        memcpy(&frame[6], payload, len);

    uint16_t crc = cps_crc_ccitt(frame, 0, 6 + len);
    frame[6 + len]     = (uint8_t)(crc >> 8);
    frame[6 + len + 1] = (uint8_t)(crc & 0xFF);

    uart_send_buf(UART4, frame, 8 + len);
}

/* Frame RX: try to parse a complete frame from rx_buf ------------ */

static int cps_try_parse_frame(uint8_t *cmd, uint8_t *payload,
                               uint8_t *payload_len)
{
    if (rx_pos < 8)
        return 0;  /* need at least header + cmd + len + crc */

    /* Scan for frame header 0xA5 */
    uint16_t start = 0;
    while (start < rx_pos && rx_buf[start] != CPS_FRAME_HEADER)
        start++;

    if (start > 0) {
        /* Discard bytes before header */
        memmove(rx_buf, &rx_buf[start], rx_pos - start);
        rx_pos -= start;
    }

    if (rx_pos < 8)
        return 0;

    uint8_t data_len = rx_buf[5];
    uint16_t frame_len = 6 + data_len + 2;

    if (rx_pos < frame_len)
        return 0;  /* incomplete frame */

    /* Validate CRC */
    uint16_t expected = cps_crc_ccitt(rx_buf, 0, 6 + data_len);
    uint16_t received = ((uint16_t)rx_buf[6 + data_len] << 8) |
                         rx_buf[7 + data_len];

    if (expected != received) {
        /* CRC mismatch - discard this frame header, try again */
        memmove(rx_buf, &rx_buf[1], rx_pos - 1);
        rx_pos--;
        return 0;
    }

    *cmd = rx_buf[4];
    *payload_len = data_len;
    if (data_len > 0)
        memcpy(payload, &rx_buf[6], data_len);

    /* Remove parsed frame from buffer */
    memmove(rx_buf, &rx_buf[frame_len], rx_pos - frame_len);
    rx_pos -= frame_len;

    return 1;
}

/* Process a received command -------------------------------------- */

static void cps_handle_command(uint8_t cmd, const uint8_t *payload,
                               uint8_t len)
{
    switch (cmd) {
    case CPS_CMD_READ: {
        /* payload: [AddrH][AddrM][AddrL][BlockLen] - 3-byte address for 2MB flash.
         * OEM programming_handler @ 0x08006A68 supports full W25Q16 range. */
        if (len < 4) break;
        uint32_t addr = ((uint32_t)payload[0] << 16) |
                        ((uint32_t)payload[1] << 8) | payload[2];
        uint8_t  block_len = payload[3];
        if (block_len > CPS_PACKET_SIZE)
            block_len = CPS_PACKET_SIZE;

        uint8_t data[CPS_PACKET_SIZE];
        spi_flash_read(addr, data, block_len);
        cps_send_frame(CPS_CMD_READ, data, block_len);
        break;
    }

    case CPS_CMD_WRITE: {
        /* payload: [AddrH][AddrM][AddrL][Data...] - 3-byte address */
        if (len < 4) break;
        uint32_t addr = ((uint32_t)payload[0] << 16) |
                        ((uint32_t)payload[1] << 8) | payload[2];
        uint16_t data_len = len - 3;

        spi_flash_write_page(addr, &payload[3], data_len);

        /* ACK: echo command with zero-length payload */
        cps_send_frame(CPS_CMD_WRITE, NULL, 0);
        break;
    }

    case CPS_CMD_ERASE: {
        /* payload: [AddrH][AddrM][AddrL] - 3-byte address */
        if (len < 3) break;
        uint32_t addr = ((uint32_t)payload[0] << 16) |
                        ((uint32_t)payload[1] << 8) | payload[2];

        spi_flash_erase_4k(addr);

        cps_send_frame(CPS_CMD_ERASE, NULL, 0);
        break;
    }

    case CPS_CMD_READ_END:
    case CPS_CMD_WRITE_END:
        /* Session complete - return to idle */
        cps_state = CPS_STATE_DONE;
        break;

    default:
        break;
    }
}

/* Public API ------------------------------------------------------ */

void cps_init(void)
{
    cps_state       = CPS_STATE_IDLE;
    handshake_count = 0;
    rx_pos          = 0;
}

int cps_is_active(void)
{
    return (cps_state == CPS_STATE_HANDSHAKE ||
            cps_state == CPS_STATE_TRANSFER);
}

int cps_poll(void)
{
    /* Drain UART4 ring buffer into local frame buffer */
    while (uart_cps_rx_available() && rx_pos < sizeof(rx_buf)) {
        int16_t ch = uart_cps_rx_read();
        if (ch >= 0)
            rx_buf[rx_pos++] = (uint8_t)ch;
    }

    switch (cps_state) {
    case CPS_STATE_IDLE:
        /* Check for handshake bytes arriving on UART4 */
        if (rx_pos > 0) {
            handshake_count += rx_pos;
            rx_pos = 0;

            if (handshake_count >= CPS_HANDSHAKE_COUNT) {
                /* Enter programming mode */
                bk4829_write_reg(0, 0x51, 0x0300);  /* mute RF */
                cps_state = CPS_STATE_HANDSHAKE;
                handshake_count = 0;

                /* Send model identification response (12 bytes) */
                cps_send_frame(CPS_CMD_READ,
                               (const uint8_t *)model_string, 12);
            }
        }
        break;

    case CPS_STATE_HANDSHAKE:
        /* Wait for first real command frame after handshake */
        cps_state = CPS_STATE_TRANSFER;
        /* fall through */

    case CPS_STATE_TRANSFER: {
        uint8_t cmd, payload[CPS_PACKET_SIZE], payload_len;
        while (cps_try_parse_frame(&cmd, payload, &payload_len)) {
            cps_handle_command(cmd, payload, payload_len);
        }
        break;
    }

    case CPS_STATE_DONE:
        /* Restore normal operation - unmute RF that was muted at session start.
         * OEM CPS handler (@ 0x08006A68) restores RF on session end. */
        bk4829_write_reg(0, 0x51, 0x0000);  /* unmute RF */
        cps_state = CPS_STATE_IDLE;
        rx_pos = 0;
        return 0;
    }

    return cps_is_active();
}
