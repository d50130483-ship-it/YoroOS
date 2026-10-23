/* YoroOS 0.2 - Native Microsoft Hyper-V Generation 2 VMBus Synthetic Mouse Driver
 *
 * Binds to Hyper-V's Synthetic HID VSC channel ({CFA8B69E-5B4A-4CC0-B98B-8BA1A1F3F95A})
 * via MsvmPkg's EFI_EMCL_PROTOCOL, negotiates SynthHid protocol v2.0, parses the real
 * HID report descriptor dynamically (X, Y, Buttons, Wheel), and delivers seamless
 * host-integrated zero-capture cursor tracking in Hyper-V Generation 2 VMs.
 */
#include "kernel.h"
#include "efi.h"

static EFI_GUID gEfiDevicePathGuid = {
    0x09576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};
static EFI_GUID gEfiDriverBindingGuid = {
    0x18a031ab, 0xb443, 0x4d1a, {0xa5, 0xc0, 0x0c, 0x09, 0x26, 0x1e, 0x9f, 0x71}
};
static EFI_GUID gHvVmbusDevPathGuid = {
    0x9b17e5a2, 0x0891, 0x42dd, {0xb6, 0x53, 0x80, 0xb5, 0xc2, 0x28, 0x09, 0xba}
};
static EFI_GUID gHvEmclProtocolGuid = {
    0x64f8b69a, 0x2fe8, 0x475d, {0x8d, 0xba, 0x46, 0x38, 0xc6, 0xc8, 0xa5, 0xae}
};
static EFI_GUID gHvEmclTagProtocolGuid = {
    0xd84ec320, 0x6d36, 0x4bdb, {0x8d, 0x4c, 0x8a, 0x02, 0x05, 0x9c, 0x33, 0xe1}
};
/* Linux drivers/hid/hid-hyperv.c HV_MOUSE_GUID: {cfa8b69e-5b4a-4cc0-b98b-8ba1a1f3f95a} */
static EFI_GUID gHvSynthMouseGuid = {
    0xcfa8b69e, 0x5b4a, 0x4cc0, {0xb9, 0x8b, 0x8b, 0xa1, 0xa1, 0xf3, 0xf9, 0x5a}
};

#define SYNTH_HID_PROTOCOL_REQUEST        0U
#define SYNTH_HID_PROTOCOL_RESPONSE       1U
#define SYNTH_HID_INITIAL_DEVICE_INFO     2U
#define SYNTH_HID_INITIAL_DEVICE_INFO_ACK 3U
#define SYNTH_HID_INPUT_REPORT            4U
#define SYNTHHID_VERSION_2_0              0x00020000U

#pragma pack(push, 1)
typedef struct {
    u32 type;
    u32 size;
} SynthHidHdr;

typedef struct {
    SynthHidHdr hdr;
    u32 version;
} SynthHidProtoReq;

typedef struct {
    SynthHidHdr hdr;
    u32 version;
    u8  approved;
} SynthHidProtoResp;

typedef struct {
    SynthHidHdr hdr;
    u32 dev_info_size;
    u16 vendor;
    u16 product;
    u16 version;
    u16 reserved[11];
    /* followed immediately by USB HID descriptor (bLength >= 9, wDescriptorLength @ +7) + report descriptor */
    u8  hid_desc_bLength;
    u8  hid_desc_bType;
    u16 hid_bcd;
    u8  hid_country;
    u8  hid_num_desc;
    u8  rpt_desc_type;
    u16 rpt_desc_len;
} SynthHidDevInfo;

typedef struct {
    SynthHidHdr hdr;
    u8 reserved;
} SynthHidDevInfoAck;
#pragma pack(pop)

static EFI_EMCL_PROTOCOL *g_hv_emcl = NULL;
static volatile int g_hv_approved = 0;
static volatile int g_hv_ready = 0;
static volatile u32 g_hv_seq = 0;
static volatile u32 g_hv_x = 16384;
static volatile u32 g_hv_y = 16384;
static volatile u32 g_hv_btns = 0;
static volatile i32 g_hv_wheel = 0;

/* Parsed HID report bit layout (defaults match standard Microsoft 045E:0621 report) */
static u32 g_btn_bit = 0, g_btn_cnt = 3;
static u32 g_x_bit = 8,   g_x_size = 16, g_x_max = 32767;
static u32 g_y_bit = 24,  g_y_size = 16, g_y_max = 32767;
static u32 g_w_bit = 40,  g_w_size = 8;
static int g_has_report_id = 0;

static u32 extract_bits_u(const u8 *buf, u32 total_bytes, u32 bit_off, u32 bit_len) {
    u32 val = 0, i;
    if (bit_len == 0 || bit_len > 31) return 0;
    for (i = 0; i < bit_len; i++) {
        u32 b = bit_off + i;
        if ((b >> 3) < total_bytes && (buf[b >> 3] & (1U << (b & 7))))
            val |= (1U << i);
    }
    return val;
}

static i32 extract_bits_s(const u8 *buf, u32 total_bytes, u32 bit_off, u32 bit_len) {
    u32 u = extract_bits_u(buf, total_bytes, bit_off, bit_len);
    if (bit_len > 0 && bit_len < 32 && (u & (1U << (bit_len - 1))))
        u |= (~0U << bit_len);
    return (i32)u;
}

static void parse_hid_report_desc(const u8 *rd, u32 len) {
    u32 pos = 0, cur_bit = 0;
    u32 usage_page = 0, report_size = 0, report_count = 0, log_max = 32767;
    u32 usages[16];
    u32 n_usages = 0;

    while (pos < len) {
        u8 b = rd[pos++];
        u32 sz, tag, type, val = 0;
        if (b == 0xFE) {
            if (pos + 2 > len) break;
            pos += 2 + rd[pos];
            continue;
        }
        sz = b & 3U;
        if (sz == 3U) sz = 4U;
        type = (b >> 2) & 3U;
        tag = (b >> 4) & 0xFU;
        if (pos + sz > len) break;
        if (sz == 1) val = rd[pos];
        else if (sz == 2) val = (u32)rd[pos] | ((u32)rd[pos + 1] << 8);
        else if (sz == 4) val = (u32)rd[pos] | ((u32)rd[pos + 1] << 8) | ((u32)rd[pos + 2] << 16) | ((u32)rd[pos + 3] << 24);
        pos += sz;

        if (type == 1) { /* Global */
            if (tag == 0) usage_page = val;
            else if (tag == 2) log_max = val;
            else if (tag == 7) report_size = val;
            else if (tag == 8) g_has_report_id = 1;
            else if (tag == 9) report_count = val;
        } else if (type == 2) { /* Local */
            if (tag == 0 && n_usages < 16) usages[n_usages++] = val;
        } else if (type == 0) { /* Main */
            if (tag == 8) { /* Input */
                if ((val & 1U) == 0) { /* Data field (not constant padding) */
                    if (usage_page == 0x09) { /* Buttons */
                        g_btn_bit = cur_bit;
                        g_btn_cnt = report_count;
                    } else if (usage_page == 0x01) { /* Generic Desktop */
                        u32 i;
                        for (i = 0; i < report_count; i++) {
                            u32 u = (i < n_usages) ? usages[i] : (n_usages ? usages[n_usages - 1] : 0);
                            if (u == 0x30) { g_x_bit = cur_bit + i * report_size; g_x_size = report_size; if (log_max > 0) g_x_max = log_max; }
                            else if (u == 0x31) { g_y_bit = cur_bit + i * report_size; g_y_size = report_size; if (log_max > 0) g_y_max = log_max; }
                            else if (u == 0x38) { g_w_bit = cur_bit + i * report_size; g_w_size = report_size; }
                        }
                    }
                }
                cur_bit += report_size * report_count;
                n_usages = 0;
            } else if (tag == 10 || tag == 12) {
                n_usages = 0;
            }
        }
    }
    if (g_has_report_id) {
        g_btn_bit += 8; g_x_bit += 8; g_y_bit += 8; g_w_bit += 8;
    }
}

typedef struct { u32 x, y, btns; i32 wheel; } HvMouseSample;
static volatile HvMouseSample g_hv_ring[64];

static void EFIAPI hv_mouse_recv_cb(
    void *ReceiveContext,
    void *PacketContext,
    void *Buffer,
    uint32_t BufferLength,
    uint16_t TransferPageSetId,
    uint32_t RangeCount,
    EFI_TRANSFER_RANGE *Ranges)
{
    UNUSED(ReceiveContext); UNUSED(TransferPageSetId); UNUSED(RangeCount); UNUSED(Ranges);
    if (Buffer && BufferLength >= sizeof(SynthHidHdr)) {
        const SynthHidHdr *hdr = (const SynthHidHdr *)Buffer;
        if (hdr->type == SYNTH_HID_PROTOCOL_RESPONSE && BufferLength >= sizeof(SynthHidProtoResp)) {
            const SynthHidProtoResp *resp = (const SynthHidProtoResp *)Buffer;
            g_hv_approved = resp->approved ? 1 : 0;
        } else if (hdr->type == SYNTH_HID_INITIAL_DEVICE_INFO && BufferLength >= sizeof(SynthHidDevInfo)) {
            const SynthHidDevInfo *info = (const SynthHidDevInfo *)Buffer;
            u32 desc_off = (u32)offsetof(SynthHidDevInfo, hid_desc_bLength) + info->hid_desc_bLength;
            if (info->hid_desc_bLength >= 9 && desc_off + info->rpt_desc_len <= BufferLength) {
                parse_hid_report_desc((const u8 *)Buffer + desc_off, info->rpt_desc_len);
            }
            if (g_hv_emcl) {
                SynthHidDevInfoAck ack;
                ack.hdr.type = SYNTH_HID_INITIAL_DEVICE_INFO_ACK;
                ack.hdr.size = 1;
                ack.reserved = 0;
                g_hv_emcl->SendPacket(g_hv_emcl, &ack, sizeof(ack), NULL, 0, NULL, NULL);
            }
            g_hv_ready = 1;
        } else if (hdr->type == SYNTH_HID_INPUT_REPORT && BufferLength > sizeof(SynthHidHdr)) {
            const u8 *rpt = (const u8 *)Buffer + sizeof(SynthHidHdr);
            u32 rlen = BufferLength - (u32)sizeof(SynthHidHdr);
            if (hdr->size < rlen) rlen = hdr->size;
            if (rlen >= 5) {
                g_hv_btns = extract_bits_u(rpt, rlen, g_btn_bit, g_btn_cnt);
                g_hv_x = extract_bits_u(rpt, rlen, g_x_bit, g_x_size);
                g_hv_y = extract_bits_u(rpt, rlen, g_y_bit, g_y_size);
                if ((g_w_bit + g_w_size) <= rlen * 8U)
                    g_hv_wheel += extract_bits_s(rpt, rlen, g_w_bit, g_w_size);
                {
                    u32 idx = g_hv_seq & 63U;
                    g_hv_ring[idx].x = g_hv_x;
                    g_hv_ring[idx].y = g_hv_y;
                    g_hv_ring[idx].btns = g_hv_btns;
                    g_hv_ring[idx].wheel = g_hv_wheel;
                    g_hv_wheel = 0;
                    g_hv_seq++;
                }
            }
        }
    }
    if (g_hv_emcl)
        g_hv_emcl->CompletePacket(g_hv_emcl, PacketContext, Buffer, BufferLength);
}

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b) {
    return memcmp(a, b, sizeof(EFI_GUID)) == 0;
}

int hv_mouse_init(EFI_HANDLE image_handle, EFI_BOOT_SERVICES *bs) {
    UINTN count = 0, i;
    EFI_HANDLE *handles = NULL;
    EFI_HANDLE mouse_handle = NULL;
    EFI_STATUS st;

    UNUSED(image_handle);
    st = bs->LocateHandleBuffer(ByProtocol, &gEfiDevicePathGuid, NULL, &count, &handles);
    if (EFI_ERROR(st) || !handles) return 0;

    for (i = 0; i < count && !mouse_handle; i++) {
        EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
        if (EFI_ERROR(bs->HandleProtocol(handles[i], &gEfiDevicePathGuid, (void **)&dp)) || !dp)
            continue;
        while (dp->Type != 0x7F) {
            u16 len = (u16)dp->Length[0] | ((u16)dp->Length[1] << 8);
            if (len < 4) break;
            if (dp->Type == 0x01 && dp->SubType == 0x04 && len >= sizeof(HV_VMBUS_DEVICE_PATH)) {
                HV_VMBUS_DEVICE_PATH *vdp = (HV_VMBUS_DEVICE_PATH *)dp;
                if (guid_eq(&vdp->VendorGuid, &gHvVmbusDevPathGuid) &&
                    guid_eq(&vdp->InterfaceType, &gHvSynthMouseGuid)) {
                    mouse_handle = handles[i];
                    break;
                }
            }
            dp = (EFI_DEVICE_PATH_PROTOCOL *)((u8 *)dp + len);
        }
    }
    bs->FreePool(handles);
    if (!mouse_handle) return 0;

    klog("[hv-mouse] Found Hyper-V VMBus Synthetic Mouse channel handle=%p", mouse_handle);
    bs->ConnectController(mouse_handle, NULL, NULL, 1);

    st = bs->HandleProtocol(mouse_handle, &gHvEmclProtocolGuid, (void **)&g_hv_emcl);
    if (EFI_ERROR(st) || !g_hv_emcl) {
        EFI_HANDLE emcl_drv = NULL;
        UINTN hsz = sizeof(emcl_drv);
        if (!EFI_ERROR(bs->LocateHandle(ByProtocol, &gHvEmclTagProtocolGuid, NULL, &hsz, &emcl_drv)) && emcl_drv) {
            EFI_DRIVER_BINDING_PROTOCOL *drv = NULL;
            if (!EFI_ERROR(bs->HandleProtocol(emcl_drv, &gEfiDriverBindingGuid, (void **)&drv)) && drv) {
                drv->Start(drv, mouse_handle, NULL);
            }
        }
        st = bs->HandleProtocol(mouse_handle, &gHvEmclProtocolGuid, (void **)&g_hv_emcl);
    }
    if (EFI_ERROR(st) || !g_hv_emcl) {
        klog("[hv-mouse] Failed to attach EFI_EMCL_PROTOCOL (status=0x%llx)", (u64)st);
        g_hv_emcl = NULL;
        return 0;
    }

    st = g_hv_emcl->SetReceiveCallback(g_hv_emcl, hv_mouse_recv_cb, NULL, TPL_CALLBACK);
    if (EFI_ERROR(st)) { g_hv_emcl = NULL; return 0; }

    st = g_hv_emcl->StartChannel(g_hv_emcl, 10, 10);
    if (EFI_ERROR(st)) { g_hv_emcl = NULL; return 0; }

    {
        SynthHidProtoReq req;
        int wait_loops;
        req.hdr.type = SYNTH_HID_PROTOCOL_REQUEST;
        req.hdr.size = 4;
        req.version = SYNTHHID_VERSION_2_0;
        st = g_hv_emcl->SendPacket(g_hv_emcl, &req, sizeof(req), NULL, 0, NULL, NULL);
        if (EFI_ERROR(st)) {
            g_hv_emcl->StopChannel(g_hv_emcl);
            g_hv_emcl = NULL;
            return 0;
        }
        for (wait_loops = 0; wait_loops < 200 && !g_hv_ready; wait_loops++)
            bs->Stall(2000);
    }

    klog("[hv-mouse] Ready=%d approved=%d range=(%u x %u)",
         g_hv_ready, g_hv_approved, g_x_max, g_y_max);
    return g_hv_ready ? 1 : 0;
}

int hv_mouse_poll(u32 *last_seq, int screen_w, int screen_h,
                  int *out_x, int *out_y, int *out_btns, int *out_wheel) {
    u32 head = g_hv_seq;
    u32 idx;
    if (!g_hv_ready || head == *last_seq) return 0;
    if ((head - *last_seq) > 64U) *last_seq = head - 64U;
    idx = (*last_seq) & 63U;
    *last_seq += 1U;
    *out_x = g_x_max ? (int)(((u64)g_hv_ring[idx].x * (u64)(screen_w - 1)) / g_x_max) : (int)g_hv_ring[idx].x;
    *out_y = g_y_max ? (int)(((u64)g_hv_ring[idx].y * (u64)(screen_h - 1)) / g_y_max) : (int)g_hv_ring[idx].y;
    *out_btns = (int)g_hv_ring[idx].btns;
    *out_wheel = (int)g_hv_ring[idx].wheel;
    return 1;
}
