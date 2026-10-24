/* Host test for the HID descriptor parser used by the Hyper-V mouse driver.
 * Built and run by `make test`. */
#include <stdio.h>
#include <string.h>
#include "hid.h"

static int failures = 0;

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

/* Absolute 3-button mouse: buttons (3 bits + 5 padding), X and Y 16-bit 0..32767, wheel 8-bit relative. */
static const u8 ABS_MOUSE[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x00, 0x26, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0
};

/* Same mouse, but every report starts with report ID 1. */
static const u8 ABS_MOUSE_ID[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
    0x85, 0x01,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x00, 0x26, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0xC0, 0xC0
};

static void test_absolute_layout(void) {
    HidMouseLayout l;
    int rc = hid_parse_mouse(ABS_MOUSE, sizeof(ABS_MOUSE), &l);
    CHECK(rc == 0, "absolute mouse: X and Y found");
    CHECK(!l.has_report_id, "absolute mouse: no report ID");
    CHECK(l.btn_bit == 0 && l.btn_size == 3, "absolute mouse: buttons at bit 0, 3 bits");
    CHECK(l.x_bit == 8 && l.x_size == 16, "absolute mouse: X at bit 8, 16 bits");
    CHECK(l.y_bit == 24 && l.y_size == 16, "absolute mouse: Y at bit 24, 16 bits");
    CHECK(l.x_max == 32767 && l.y_max == 32767, "absolute mouse: logical max 32767");
    CHECK(l.wheel_bit == 40 && l.wheel_size == 8, "absolute mouse: wheel at bit 40, 8 bits");
}

static void test_report_id_layout(void) {
    HidMouseLayout l;
    int rc = hid_parse_mouse(ABS_MOUSE_ID, sizeof(ABS_MOUSE_ID), &l);
    CHECK(rc == 0, "report-ID mouse: X and Y found");
    CHECK(l.has_report_id, "report-ID mouse: flag set");
    CHECK(l.btn_bit == 8, "report-ID mouse: buttons shifted by the 8-bit ID");
    CHECK(l.x_bit == 16 && l.y_bit == 32, "report-ID mouse: X and Y shifted by the ID");
}

static void test_no_pointer(void) {
    static const u8 KBD[] = { 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0xC0 };
    HidMouseLayout l;
    CHECK(hid_parse_mouse(KBD, sizeof(KBD), &l) != 0, "keyboard descriptor: no pointer found");
}

static void test_extract(void) {
    /* buttons = 0b101 (left, middle), X = 0x1234, Y = 0x5678, wheel = 0xFF (-1) */
    static const u8 rep[6] = { 0x05, 0x34, 0x12, 0x78, 0x56, 0xFF };
    HidMouseLayout l;
    hid_parse_mouse(ABS_MOUSE, sizeof(ABS_MOUSE), &l);
    CHECK(hid_extract_bits(rep, 6, l.btn_bit, l.btn_size) == 5, "extract buttons");
    CHECK(hid_extract_bits(rep, 6, l.x_bit, l.x_size) == 0x1234, "extract X");
    CHECK(hid_extract_bits(rep, 6, l.y_bit, l.y_size) == 0x5678, "extract Y");
    CHECK(hid_extract_bits(rep, 6, l.wheel_bit, l.wheel_size) == 0xFF, "extract wheel");
    /* Field straddling a byte edge: 8 bits from bit 4 of {0x05, 0x34, ...} = (0x05 >> 4) | (0x34 << 4) & 0xFF = 0x40. */
    CHECK(hid_extract_bits(rep, 6, 4, 8) == 0x40, "extract across a byte boundary");
    /* Last bit of the buffer: bit 47 is bit 7 of 0xFF = 1, bits past the end read as 0. */
    CHECK(hid_extract_bits(rep, 6, 47, 16) == 1, "extract at the end of the buffer");
}

int main(void) {
    test_absolute_layout();
    test_report_id_layout();
    test_no_pointer();
    test_extract();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("hid: all checks passed\n");
    return 0;
}
