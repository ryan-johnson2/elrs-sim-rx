// CRSF (Crossfire / ExpressLRS serial protocol) parser and frame builder.
// Hardware independent: feed it bytes and a millisecond clock.
//
// Frame layout on the wire:
//   [sync][len][type][payload ...][crc8]
//   len = number of bytes after the len field (type + payload + crc)
//   crc8 = CRC-8 DVB-S2 over type + payload
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CRSF_SYNC_BYTE          0xC8   // sync byte used on the RX's serial port
#define CRSF_ADDR_RECEIVER      0xEC   // address used for raw commands to the RX
#define CRSF_MAX_FRAME_LEN      64
#define CRSF_NUM_CHANNELS       16

#define CRSF_CH_MIN             172
#define CRSF_CH_MID             992
#define CRSF_CH_MAX             1811

#define CRSF_TYPE_LINK_STATS    0x14
#define CRSF_TYPE_RC_CHANNELS   0x16
#define CRSF_TYPE_COMMAND       0x32

// Inter-byte gap after which a partial frame is discarded (resync).
#define CRSF_INTER_BYTE_TIMEOUT_MS 2

typedef struct {
    uint8_t uplink_rssi1;      // dBm * -1
    uint8_t uplink_rssi2;
    uint8_t uplink_lq;         // 0..100 (%)
    int8_t  uplink_snr;        // dB
    uint8_t active_antenna;
    uint8_t rf_mode;
    uint8_t uplink_tx_power;
    uint8_t downlink_rssi;
    uint8_t downlink_lq;
    int8_t  downlink_snr;
} crsf_link_stats_t;

typedef struct {
    // parser state
    uint8_t  buf[CRSF_MAX_FRAME_LEN];
    uint8_t  pos;
    uint8_t  frame_len;        // value of the len field once known
    uint32_t last_byte_ms;

    // decoded state
    uint16_t channels[CRSF_NUM_CHANNELS];   // raw 11-bit, 172..1811
    crsf_link_stats_t link;
    bool     have_rc;          // at least one RC frame has been decoded
    bool     have_link;        // at least one link stats frame has been decoded
    uint32_t last_rc_ms;       // time of last RC channels frame
    uint32_t last_link_ms;     // time of last link stats frame
    uint32_t last_frame_ms;    // time of last valid frame of any type

    // counters
    uint32_t frames_ok;
    uint32_t frames_crc_err;
    uint32_t frames_timeout;
    uint32_t bytes_discarded;
} crsf_t;

void crsf_init(crsf_t *c);

// Feed one byte. Returns the frame type if this byte completed a valid frame,
// 0 otherwise.
uint8_t crsf_feed(crsf_t *c, uint8_t byte, uint32_t now_ms);

// Convert a raw channel value to the usual 988..2012 us scale.
uint16_t crsf_channel_to_us(uint16_t ch);

// Build a raw two-character command frame addressed to the receiver, e.g.
// "bd" (enter bind), "bl" (reboot to bootloader). Writes to out (>= 6 bytes),
// returns length.
size_t crsf_build_rx_command(uint8_t *out, char c0, char c1);

// Build the raw "mm" frame that sets the receiver's model-match ID.
// id 0-63 enables model match with that ID, CRSF_MODEL_ID_OFF disables it.
// Writes to out (>= 7 bytes), returns length.
#define CRSF_MODEL_ID_OFF  0xFF
#define CRSF_MODEL_ID_MAX  63
size_t crsf_build_rx_model_id(uint8_t *out, uint8_t id);

// Build an RC channels frame from raw channel values (used by tests and by the
// optional CRSF-out path). out must hold at least 26 bytes. Returns length.
size_t crsf_build_rc_frame(uint8_t *out, const uint16_t ch[CRSF_NUM_CHANNELS]);

// Build a link statistics frame. out must hold at least 14 bytes.
size_t crsf_build_link_stats_frame(uint8_t *out, const crsf_link_stats_t *ls);
