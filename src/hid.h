/* HID report descriptor helpers: find where the pointer fields sit in an input report. */
#ifndef YORO_HID_H
#define YORO_HID_H

#include "efi.h"

typedef struct {
    int has_report_id;
    int btn_bit, btn_size;      /* button bits (usage page 0x09) */
    int x_bit, x_size;          /* X axis (page 0x01, usage 0x30) */
    u32 x_max;                  /* logical maximum of X */
    int y_bit, y_size;          /* Y axis (page 0x01, usage 0x31) */
    u32 y_max;                  /* logical maximum of Y */
    int wheel_bit, wheel_size;  /* wheel (page 0x01, usage 0x38), size 0 if absent */
} HidMouseLayout;

/* Parses a report descriptor. Returns 0 when both X and Y were found, -1 otherwise. */
int hid_parse_mouse(const u8 *desc, int len, HidMouseLayout *out);

/* Reads bit_cnt bits starting at bit_pos (little-endian bit order, as HID defines). */
u32 hid_extract_bits(const u8 *buf, int byte_len, int bit_pos, int bit_cnt);

#endif
