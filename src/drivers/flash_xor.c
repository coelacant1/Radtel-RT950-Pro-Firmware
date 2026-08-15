/*
 * flash_xor.c - Flash data XOR obfuscation for RT-950 Pro
 *
 * Reimplements OEM eeprom_block_copy @ 0x0801F8D0.
 * Selective byte-by-byte XOR with rotating 4-byte key.
 *
 * OEM callers:
 *   keypad_eeprom_save  @ 0x0800647C (write path, conditional)
 *   flash_data_load     @ 0x08007358 (read path, address-selective)
 *
 * Applied to: Settings A (0x8000), Extended (0xB000)
 * Not applied to: Settings B (0x9000), Calibration (0xF000)
 */

#include "drivers/flash_xor.h"

const uint8_t flash_xor_key[FLASH_XOR_KEY_SIZE] = { 0x41, 0x41, 0x41, 0x41 };

void flash_xor_copy(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    uint8_t ki = 0;     /* key index, cycles 0-3 */

    for (uint16_t i = 0; i < len; i++) {
        uint8_t kb = flash_xor_key[ki];
        uint8_t sb = src[i];
        /* Narrow the inverse to uint8_t in its own object. Written inline as
         * (uint8_t)(kb ^ 0xFF) it is still an int-promoted complement at the
         * point of comparison, which -Werror=sign-compare rejects on gcc 10.3.
         * Semantics are unchanged. */
        uint8_t kb_inv = (uint8_t)(kb ^ 0xFFu);

        if (kb == 0x20 ||           /* key byte is space */
            sb == 0x00 ||           /* source is zero */
            sb == 0xFF ||           /* source is erased flash */
            sb == kb   ||           /* source matches key */
            sb == kb_inv)           /* source matches inverse key */
        {
            dst[i] = sb;           /* copy as-is */
        } else {
            dst[i] = sb ^ kb;     /* XOR with key byte */
        }

        ki = (ki + 1) & 3;        /* rotate key index (mod 4) */
    }
}
