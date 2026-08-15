/*
 * flash_layout.h - SPI flash memory map for RT-950 Pro
 *
 * External NOR flash: Winbond W25Q16 (2 MB / 16 Mbit)
 * Interface: SPI2 (PB12=CS, PB13=SCK, PB14=MISO, PB15=MOSI)
 *
 * Layout verified against V0.27 firmware + KDH server config JSON.
 * CPS/BLE-accessible regions: 0x000000-0x00D300 + 0x00FFFF-0x01007F
 * Firmware-internal regions: 0x00E000, 0x00F000, 0x010000+
 *
 * Channel memory: 990 channels x 32 bytes at 0x000000 (ends 0x007BC0)
 * Calibration: fixed at 0x00F000 (not wear-leveled, not CPS-accessible)
 * Settings: multiple sectors (0x008000-0x010FFF)
 */

#ifndef DRIVERS_FLASH_LAYOUT_H
#define DRIVERS_FLASH_LAYOUT_H

#include <stdint.h>

/* Flash geometry ---------------------------------------------------- */
#define FLASH_TOTAL_SIZE        0x200000    /* 2 MB */
#define FLASH_PAGE_SIZE         256         /* Bytes per write page */
#define FLASH_SECTOR_SIZE       4096        /* 4 KB erase sector */
#define FLASH_BLOCK_SIZE_32K    (32 * 1024)
#define FLASH_BLOCK_SIZE_64K    (64 * 1024)

/* Channel memory: 0x000000 - 0x007BBF (31,680 bytes) --------------- */
#define FLASH_ADDR_CHANNELS     0x000000
#define CHANNEL_RECORD_SIZE     0x20        /* 32 bytes per channel (BCD) */
#define CHANNEL_COUNT_MAX       990         /* 990 channels (verified via server) */
#define CHANNELS_PER_SECTOR     128         /* 128 x 32 = 4096 */
#define CHANNEL_SECTORS         8

/*
 * Channel record (32 bytes) - verified from KDH server configJSON:
 *
 *   Byte   Field                Encoding
 *   -----  -------------------  --------------------------------------
 *   0-3    RX frequency         Packed BCD, LE digit order (Hz/10)
 *   4-7    TX frequency         Packed BCD, LE digit order (Hz/10)
 *   8      RX CTCSS/DCS hi      Index high byte into 261-entry table
 *   9      RX CTCSS/DCS lo      Index low byte
 *   10     TX CTCSS/DCS hi      Index high byte
 *   11     TX CTCSS/DCS lo      Index low byte
 *   12     Signal Code          0-14 (DTMF group index)
 *   13     PTT-ID               0=OFF, 1=BOT, 2=EOT, 3=BOTH
 *   14     Power[3:0]           0=High, 1=Mid, 2=Low
 *          Scramble[7:4]        0=OFF, 1=ON
 *   15     RxModMode[0]         0=FM, 1=AM
 *          TxEnable[1]          0=OFF, 1=ON
 *          ScanAdd[2]           0=OFF, 1=ON
 *          BusyLock[3]          0=OFF, 1=ON
 *          Encryption[5:4]      0=OFF, 1=DCP1, 2=DCP2, 3=DCP3
 *          NarrowWide[6]        0=Wide(25k), 1=Narrow(12.5k)
 *   16-19  Reserved
 *   20-31  Channel Name         12 bytes ASCII, space-padded
 *
 *   V0.27 verification: name write @ 0x0800EFEA stores 12 bytes
 *   at record offset 0x14 (20) via three str instructions.
 */
typedef struct __attribute__((packed)) {
    uint8_t  rx_freq_bcd[4];    /* RX freq BCD */
    uint8_t  tx_freq_bcd[4];    /* TX freq BCD */
    uint8_t  rx_ctcss_hi;       /* RX CTCSS/DCS index high byte */
    uint8_t  rx_ctcss_lo;       /* RX CTCSS/DCS index low byte */
    uint8_t  tx_ctcss_hi;       /* TX CTCSS/DCS index high byte */
    uint8_t  tx_ctcss_lo;       /* TX CTCSS/DCS index low byte */
    uint8_t  signal_code;       /* DTMF group 0-14 */
    uint8_t  ptt_id;            /* 0=OFF, 1=BOT, 2=EOT, 3=BOTH */
    uint8_t  power_scramble;    /* [3:0]=power, [7:4]=scramble */
    uint8_t  flags;             /* [0]=rxmod, [1]=txen, [2]=scan, [3]=busy,
                                   [5:4]=encry, [6]=narrow */
    uint8_t  reserved[4];       /* bytes 16-19 */
    uint8_t  name[12];          /* Channel name ASCII, offset 0x14 (20) */
} channel_record_t;

/* VFO config: 0x008000 (3 VFOs x 32 bytes each) -------------------- */
#define FLASH_ADDR_VFO          0x008000
#define FLASH_ADDR_VFO_A        0x008000    /* VFO A config (16B) + settings (16B) */
#define FLASH_ADDR_VFO_A_SET    0x008010
#define FLASH_ADDR_VFO_B        0x008020    /* VFO B config */
#define FLASH_ADDR_VFO_B_SET    0x008030
#define FLASH_ADDR_VFO_C        0x008040    /* VFO C config */
#define FLASH_ADDR_VFO_C_SET    0x008050
#define VFO_COUNT               3

/*
 * VFO record (32 bytes) - DIFFERENT freq encoding from channels:
 *   +0..7   RX freq: 8 digit-per-byte (NNN.NNNNN MHz as digits)
 *   +8..9   RX CTCSS/DCS index (hi, lo)
 *   +10..11 TX CTCSS/DCS index (hi, lo)
 *   +12..15 Additional fields
 *
 * Settings at +16:
 *   +16 [3:0]=power, [7:4]=scramble
 *   +17 [0]=rxmod, [1]=narrow, [5:4]=encryption
 *   +18 band index (0=VHF, 1=UHF)
 *   +19 [3:0]=step frequency index
 *   +20..26 TX offset freq: 7 digit-per-byte (NNN.NNNN MHz)
 */
typedef struct __attribute__((packed)) {
    uint8_t  rx_freq_digits[8];  /* Digit-per-byte: [1,4,7,2,6,5,0,0] = 147.26500 */
    uint8_t  rx_ctcss_hi;
    uint8_t  rx_ctcss_lo;
    uint8_t  tx_ctcss_hi;
    uint8_t  tx_ctcss_lo;
    uint8_t  signal_code;
    uint8_t  ptt_id;
    uint8_t  power_scramble;
    uint8_t  flags;
    uint8_t  power_scramble2;    /* Settings +0 */
    uint8_t  mode_flags;         /* Settings +1 */
    uint8_t  band_index;         /* Settings +2 */
    uint8_t  step_freq;          /* Settings +3 [3:0] */
    uint8_t  offset_digits[7];   /* TX offset freq digit-per-byte */
    uint8_t  reserved[5];
} vfo_record_t;

/* System settings: 0x009000 (80+ bytes across 5 blocks) ------------- */
#define FLASH_ADDR_SETTINGS     0x009000
#define FLASH_ADDR_SETTINGS_0   0x009000    /* Squelch, PSM, VOX, backlight, etc */
#define FLASH_ADDR_SETTINGS_1   0x009010    /* Auto-lock, alarm, STE, FM, keys */
#define FLASH_ADDR_SETTINGS_2   0x009020    /* VOX delay, menu exit, side keys */
#define FLASH_ADDR_SETTINGS_3   0x009030    /* AB-UV, TX spk, numkey long-press */
#define FLASH_ADDR_SETTINGS_4   0x009040    /* Numkey 5-9 long-press */
#define SETTINGS_TOTAL_SIZE     80

/* Extended config: 0x00A000 ----------------------------------------- */
#define FLASH_ADDR_DTMF         0x00A000    /* DTMF contacts + PTT config */
#define FLASH_ADDR_SI4732       0x00B000    /* FM/AM/SSB channels + config */
/* Zone names: 0x00C000, ten slots on a 16-byte pitch.
 *
 * Confirmed against a dump of a physical RT-950 Pro running V0.29 -- 0x00C000
 * holds the OEM defaults "ZoneOne".."ZoneTen", 0xFF-padded, 0x00C0A0 onward
 * erased.
 *
 * Slot pitch (16) and name length (12) are separate values. They were one
 * FLASH_ZONE_NAME_SIZE of 16, which indexes correctly -- the pitch is what
 * indexing needs -- but is wrong as a length, so reading a slot pulls in four
 * bytes of the next one. Splitting them keeps both uses honest. Radtel's RT-900
 * header agrees: BANK_NAME_SIZE 16, commented "supports 12 bytes, stored in
 * 16". */
#define FLASH_ADDR_ZONE_NAMES   0x00C000    /* 10 zone names, 16-byte stride */
#define FLASH_ZONE_NAME_STRIDE  16          /* slot pitch */
#define FLASH_ZONE_NAME_SIZE    12          /* bytes of each slot used for text */
#define FLASH_ZONE_MAX          10
#define FLASH_ADDR_FM_NAMES     0x00D010    /* 15 FM channel names x 16B */
#define FLASH_ADDR_AM_NAMES     0x00D110    /* 15 AM channel names x 16B */
#define FLASH_ADDR_SSB_NAMES    0x00D210    /* 15 SSB channel names x 16B */
#define FLASH_ADDR_EXT_END      0x00D300    /* End of CPS-accessible extended */

/* APRS / GPS config: 0x00FFFF --------------------------------------- */
#define FLASH_ADDR_APRS         0x00FFFF    /* APRS switch, GPS, units, coords */
#define FLASH_ADDR_APRS_EXT     0x01000F    /* SSID, routing, symbol, priority */
#define FLASH_ADDR_APRS_BEACON  0x01001F    /* Beacon TX, MIC-E, TNC, fwd */
#define FLASH_ADDR_APRS_ROUTE   0x01002F    /* Custom routing SSIDs */
#define FLASH_ADDR_APRS_MSG     0x01003F    /* Custom messages */
#define FLASH_ADDR_APRS_MISC    0x01006F    /* TX data reporting, beacon popup */
#define FLASH_ADDR_APRS_END     0x01007F    /* End of APRS config */

/* Firmware-internal (NOT exposed to CPS/BLE) ------------------------ */
#define FLASH_ADDR_VFOSEL       0x00E000    /* VFO/channel select (WL) */
#define FLASH_ADDR_CALIBRATION  0x00F000    /* Calibration (fixed, factory) */
#define FLASH_CAL_SIZE          0x300       /* ~768 bytes used */
#define FLASH_CAL_FLAG_OFFSET   0x0E0       /* 0xF0E0: !=0xFF = programmed */
#define FLASH_ADDR_SYSCFG       0x010000    /* System config (WL, internal) */
#define FLASH_ADDR_VFOCFG       0x009000    /* VFO config WL (OEM: 0x9000) */
#define FLASH_ADDR_EXTCFG       0x00B000    /* Extended config WL (OEM: 0xB000) */
#define FLASH_ADDR_CONTACTS     0x011000    /* Contact/DTMF names (WL) */
/* WL record sizes and slot counts are in spi_flash.h */

/* Boot splash / logo: 0x090000 -------------------------------------- */
#define FLASH_ADDR_SPLASH       0x090000
#define FLASH_SPLASH_BLOCK      0x3C00      /* 15360 bytes per block */

/* Font tables (read-only, factory) ---------------------------------- */
#define FLASH_ADDR_FONT_ASCII_L 0x15C000    /* Large ASCII (39 B/glyph) V0.27 @ 0x08025826 */
#define FLASH_ADDR_FONT_CJK_S   0x15CF00   /* Small CJK (32 B/glyph) V0.27 lit @ 0x080258A0 */
#define FLASH_ADDR_FONT_CJK_T   0x1986C0   /* Tiny CJK (16 B/glyph) V0.27 lit @ 0x08025974 */
#define FLASH_ADDR_FONT_CJK_M   0x199E80   /* Medium CJK (24 B/glyph) V0.27 lit @ 0x08025820 */
#define FLASH_ADDR_FONT_ASCII_M 0x1C3C40   /* Medium ASCII (12 B/glyph) V0.27 lit @ 0x0802592C */
#define FLASH_ADDR_FONT_DISPLAY 0x1D35C8   /* Display font (192 B/glyph) V0.27 lit @ 0x080147C8 */
#define FLASH_ADDR_FONT_DISP_ALT 0x1D4090  /* Alternate display V0.27 lit @ 0x080147CC */

/* Wear-leveling parameters ------------------------------------------ */
#define WL_VALID_MARKER         0xA5        /* Marks valid WL entry */

/* API --------------------------------------------------------------- */

/* Channel frequency: packed BCD (4 bytes, LE digit order, value = Hz/10) */
uint32_t channel_freq_decode(const uint8_t bcd[4]);
void     channel_freq_encode(uint32_t hz, uint8_t bcd[4]);

/* VFO frequency: digit-per-byte (8 bytes, NNN.NNNNN MHz) */
uint32_t vfo_freq_decode(const uint8_t digits[8]);
void     vfo_freq_encode(uint32_t hz, uint8_t digits[8]);

/* Read a channel record from flash */
int flash_read_channel(uint16_t ch_num, channel_record_t *ch);

/* Write a channel record to flash (handles sector erase + rewrite) */
int flash_write_channel(uint16_t ch_num, const channel_record_t *ch);

/* Read/write VFO records (3 VFOs at 0x008000) */
int flash_read_vfo(uint8_t vfo_idx, vfo_record_t *vfo);
int flash_write_vfo(uint8_t vfo_idx, const vfo_record_t *vfo);

/* Read calibration data (up to FLASH_CAL_SIZE bytes) */
int flash_read_calibration(uint8_t *buf, uint16_t len);

/* Read system settings (from SETTINGS at 0x9000) */
int flash_read_settings(uint8_t *buf, uint16_t len);

/* Write system settings (erase + write at 0x9000) */
int flash_write_settings(const uint8_t *buf, uint16_t len);

#endif /* DRIVERS_FLASH_LAYOUT_H */
