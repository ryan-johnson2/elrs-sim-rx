// CRSF parameter protocol client (device ping, parameter read/write).
// This is what the ExpressLRS Lua script speaks to list and change a device's
// settings. Hardware independent: hand it complete frames, poll it for frames
// to send. One request at a time.
//
// Extended frame layout:
//   [sync][len][type][dest][origin][payload ...][crc8]
//
//   DEVICE_PING  0x28  (no payload)
//   DEVICE_INFO  0x29  name\0, serial(4), hw(4), sw(4), field count, param version
//   PARAM_ENTRY  0x2B  field id, chunks remaining, field data chunk
//   PARAM_READ   0x2C  field id, chunk number
//   PARAM_WRITE  0x2D  field id, value (big-endian)
//
// A field is sent in chunks because one frame holds at most 64 bytes. The
// reassembled field data is: parent id, type, name\0, then per-type content.
//
// ExpressLRS receivers answer these on their serial port from 4.0 onward.
// 3.x relays them to the transmitter instead, so requests time out.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CRSF_TYPE_DEVICE_PING   0x28
#define CRSF_TYPE_DEVICE_INFO   0x29
#define CRSF_TYPE_PARAM_ENTRY   0x2B
#define CRSF_TYPE_PARAM_READ    0x2C
#define CRSF_TYPE_PARAM_WRITE   0x2D

#define CRSF_ADDR_FLIGHT_CTRL   0xC8   // who we are, from the receiver's side

// Field types (low 7 bits of the type byte; bit 7 = hidden).
#define CRSF_PARAM_UINT8        0
#define CRSF_PARAM_INT8         1
#define CRSF_PARAM_UINT16       2
#define CRSF_PARAM_INT16        3
#define CRSF_PARAM_UINT32       4
#define CRSF_PARAM_INT32        5
#define CRSF_PARAM_FLOAT        8
#define CRSF_PARAM_SELECTION    9
#define CRSF_PARAM_STRING       10
#define CRSF_PARAM_FOLDER       11
#define CRSF_PARAM_INFO         12
#define CRSF_PARAM_COMMAND      13

// Command steps, written as the value of a COMMAND field.
#define CRSF_CMD_IDLE           0
#define CRSF_CMD_CLICK          1
#define CRSF_CMD_EXECUTING      2
#define CRSF_CMD_ASK_CONFIRM    3
#define CRSF_CMD_CONFIRMED      4
#define CRSF_CMD_CANCEL         5
#define CRSF_CMD_QUERY          6

#define CRSF_PARAMS_FIELD_MAX   320    // reassembled field data
#define CRSF_PARAMS_WRITE_MAX   32     // value bytes in one write
#define CRSF_PARAMS_TX_MAX      (6 + 1 + CRSF_PARAMS_WRITE_MAX)

#define CRSF_PARAMS_TRY_TIMEOUT_MS  200   // wait for a reply before resending
#define CRSF_PARAMS_MAX_TRIES       3
// After a write the device either answers by itself (commands) or says
// nothing (plain values); after this long we read the field back.
#define CRSF_PARAMS_WRITE_SETTLE_MS 150

typedef enum {
    CRSF_PARAMS_IDLE = 0,
    CRSF_PARAMS_BUSY,
    CRSF_PARAMS_DONE,
    CRSF_PARAMS_FAILED,
} crsf_params_state_t;

typedef enum {
    CRSF_PARAMS_ERR_NONE = 0,
    CRSF_PARAMS_ERR_TIMEOUT,     // no reply after all tries
    CRSF_PARAMS_ERR_OVERFLOW,    // field larger than CRSF_PARAMS_FIELD_MAX
    CRSF_PARAMS_ERR_MALFORMED,   // reply did not parse
} crsf_params_err_t;

typedef enum {
    CRSF_PARAMS_OP_NONE = 0,
    CRSF_PARAMS_OP_PING,
    CRSF_PARAMS_OP_READ,
    CRSF_PARAMS_OP_WRITE,
} crsf_params_op_t;

typedef struct {
    char     name[32];
    uint32_t serial;
    uint32_t hw_version;
    uint32_t sw_version;        // ExpressLRS: 0x00MMmmpp
    uint8_t  field_count;
    uint8_t  param_version;
} crsf_device_info_t;

typedef struct {
    uint8_t  self_addr;
    uint8_t  dev_addr;

    crsf_params_state_t state;
    crsf_params_op_t    op;
    crsf_params_err_t   err;

    uint8_t  field_id;
    uint8_t  chunk;             // next chunk number to request
    uint8_t  remaining;         // chunks remaining after the last one received
    bool     have_chunk;
    uint8_t  phase;             // write only: 0 send, 1 settle, 2 read back
    uint8_t  tries;
    bool     tx_pending;
    uint32_t sent_ms;

    uint8_t  write_val[CRSF_PARAMS_WRITE_MAX];
    uint8_t  write_len;

    crsf_device_info_t info;                    // valid after a DONE ping
    uint8_t  field[CRSF_PARAMS_FIELD_MAX + 1];  // valid after a DONE read/write
    uint16_t field_len;                         // field[field_len] is always 0
} crsf_params_t;

void crsf_params_init(crsf_params_t *p, uint8_t self_addr, uint8_t dev_addr);

// Start a request. Each returns false if one is already running (or the
// arguments are bad). The result is in p->state once it leaves BUSY.
bool crsf_params_ping(crsf_params_t *p);
bool crsf_params_read(crsf_params_t *p, uint8_t field_id);
// Writes the value, then leaves the field as the device reports it afterwards
// in p->field, exactly like a read.
bool crsf_params_write(crsf_params_t *p, uint8_t field_id, const uint8_t *value, size_t len);
void crsf_params_cancel(crsf_params_t *p);

// Call regularly. If a frame should be sent now it is written to out
// (>= CRSF_PARAMS_TX_MAX bytes) and its length returned, else 0.
size_t crsf_params_poll(crsf_params_t *p, uint32_t now_ms, uint8_t *out);

// Hand over every complete, CRC-checked frame (from the sync byte to the CRC).
// Returns true if it belonged to the running request.
bool crsf_params_on_frame(crsf_params_t *p, const uint8_t *frame, size_t len);

// Decoded view of a field. Strings point into the data passed to the parser.
typedef struct {
    uint8_t  parent;
    uint8_t  type;              // CRSF_PARAM_*
    bool     hidden;
    const char *name;
    // UINT8..INT32 and SELECTION
    int32_t  value, min, max, def;
    const char *units;          // numeric and SELECTION, "" if none
    const char *options;        // SELECTION: labels separated by ';'
    const char *text;           // STRING/INFO value, COMMAND status text
    uint8_t  step;              // COMMAND: CRSF_CMD_*
    uint8_t  timeout;           // COMMAND: in 10 ms units
    const uint8_t *children;    // FOLDER: child field ids
    uint8_t  n_children;
} crsf_param_field_t;

// Parse reassembled field data. Every string must be terminated inside
// data[0..len). For a crsf_params_t result pass p->field, p->field_len + 1,
// which tolerates a device that leaves off the last terminator.
// Returns false if the data is truncated. Types it does not know (FLOAT)
// parse with only parent, type, hidden and name filled in.
bool crsf_params_parse_field(const uint8_t *data, size_t len, crsf_param_field_t *f);
