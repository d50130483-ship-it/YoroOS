/* HID report descriptor parser for absolute and relative mice. */
#include "hid.h"

static u32 read_item_value(const u8 *d, int sz) {
    u32 v = 0;
    for (int k = 0; k < sz; k++) v |= (u32)d[k] << (8 * k);
    return v;
}

int hid_parse_mouse(const u8 *d, int len, HidMouseLayout *out) {
    u32 usage_page = 0, rep_size = 0, rep_cnt = 0;
    u32 usages[16];
    int n_usages = 0;
    u32 log_max = 32767;
    int bit_off = 0, have_x = 0, have_y = 0;
    int i = 0;

    out->has_report_id = 0;
    out->btn_bit = 0;
    out->btn_size = 0;
    out->x_bit = 0;
    out->x_size = 0;
    out->x_max = 32767;
    out->y_bit = 0;
    out->y_size = 0;
    out->y_max = 32767;
    out->wheel_bit = 0;
    out->wheel_size = 0;

    while (i < len) {
        u8 b = d[i++];
        int sz = b & 3;
        int typ = (b >> 2) & 3;
        int tag = (b >> 4) & 0xF;
        u32 val;

        if (b == 0xFE) break;           /* long item: not used by mice */
        if (sz == 3) sz = 4;
        if (i + sz > len) break;
        val = read_item_value(d + i, sz);
        i += sz;

        if (typ == 1) {                 /* global */
            if (tag == 0) usage_page = val;
            else if (tag == 2) log_max = (sz == 2 && (val & 0x8000U)) ? (u32)(0xFFFF0000U | val) : val;
            else if (tag == 7) rep_size = val;
            else if (tag == 8) { out->has_report_id = 1; bit_off += 8; }
            else if (tag == 9) rep_cnt = val;
        } else if (typ == 2) {          /* local */
            if (tag == 0 && n_usages < 16) usages[n_usages++] = val;
        } else if (typ == 0 && tag == 8) { /* main: Input */
            if (val & 1U) {
                /* Constant item: padding, skip its bits. */
                bit_off += (int)(rep_size * rep_cnt);
            } else if (usage_page == 0x09U) {
                if (out->btn_size == 0) {  /* first data button block only */
                    out->btn_bit = bit_off;
                    out->btn_size = (int)(rep_size * rep_cnt);
                }
                bit_off += (int)(rep_size * rep_cnt);
            } else {
                for (u32 k = 0; k < rep_cnt; k++) {
                    u32 u = k < (u32)n_usages ? usages[k] : (n_usages ? usages[n_usages - 1] : 0);
                    if (usage_page == 0x01U && u == 0x30U) {
                        out->x_bit = bit_off; out->x_size = (int)rep_size; out->x_max = log_max;
                        have_x = 1;
                    } else if (usage_page == 0x01U && u == 0x31U) {
                        out->y_bit = bit_off; out->y_size = (int)rep_size; out->y_max = log_max;
                        have_y = 1;
                    } else if (usage_page == 0x01U && u == 0x38U) {
                        out->wheel_bit = bit_off; out->wheel_size = (int)rep_size;
                    }
                    bit_off += (int)rep_size;
                }
            }
        }
        if (typ == 0) n_usages = 0;     /* local items apply to the next main item only */
    }

    if (out->x_max == 0) out->x_max = 32767;
    if (out->y_max == 0) out->y_max = 32767;
    return (have_x && have_y) ? 0 : -1;
}

u32 hid_extract_bits(const u8 *buf, int byte_len, int bit_pos, int bit_cnt) {
    u64 acc = 0;
    int b0 = bit_pos >> 3;
    if (bit_cnt <= 0 || bit_cnt > 32) return 0;
    for (int k = 0; k < 8 && b0 + k < byte_len; k++)
        acc |= (u64)buf[b0 + k] << (8 * k);
    acc >>= (bit_pos & 7);
    acc &= (1ULL << bit_cnt) - 1ULL;
    return (u32)acc;
}
