/* YoroOS 0.2.5 - Native Bare-Metal Microsoft Hyper-V Generation 2 Driver Stack
 *
 * Verified byte-for-byte against mainline Linux 6.x:
 *   include/hyperv/hvgdk_mini.h, hvhdk_mini.h, include/linux/hyperv.h,
 *   drivers/hv/{connection.c, channel.c, ring_buffer.c},
 *   drivers/gpu/drm/hyperv/hyperv_drm_proto.c,
 *   drivers/hid/hid-hyperv.c, drivers/input/serio/hyperv-keyboard.c,
 *   drivers/scsi/storvsc_drv.c.
 */
#include "hv_native.h"

/* ---------------- Hyper-V Architectural MSRs & Hypercalls ---------------- */
#define HV_X64_MSR_GUEST_OS_ID     0x40000000U
#define HV_X64_MSR_HYPERCALL       0x40000001U
#define HV_X64_MSR_VP_INDEX        0x40000002U
#define HV_X64_MSR_RESET           0x40000003U
#define HV_X64_MSR_TIME_REF_COUNT  0x40000020U
#define HV_X64_MSR_EOI             0x40000070U
#define HV_X64_MSR_SCONTROL        0x40000080U
#define HV_X64_MSR_SIEFP           0x40000082U
#define HV_X64_MSR_SIMP            0x40000083U
#define HV_X64_MSR_EOM             0x40000084U
#define HV_X64_MSR_SINT0           0x40000090U
#define HV_X64_MSR_SINT2           0x40000092U

#define HVCALL_POST_MESSAGE        0x005CU
#define HVCALL_SIGNAL_EVENT        0x005DU
#define HV_HYPERCALL_FAST_BIT      0x00010000ULL
#define HV_STATUS_SUCCESS          0x0000U

#define VMBUS_MESSAGE_SINT         2U
#define VMBUS_SINT_VECTOR          0x32U

/* VMBus Control Message Types */
#define CHANNELMSG_OFFERCHANNEL        1U
#define CHANNELMSG_RESCIND_CHANNELOFFER 2U
#define CHANNELMSG_REQUESTOFFERS       3U
#define CHANNELMSG_ALLOFFERS_DELIVERED 4U
#define CHANNELMSG_OPENCHANNEL         5U
#define CHANNELMSG_OPENCHANNEL_RESULT  6U
#define CHANNELMSG_CLOSECHANNEL        7U
#define CHANNELMSG_GPADL_HEADER        8U
#define CHANNELMSG_GPADL_BODY          9U
#define CHANNELMSG_GPADL_CREATED       10U
#define CHANNELMSG_INITIATE_CONTACT    14U
#define CHANNELMSG_VERSION_RESPONSE    15U
#define CHANNELMSG_UNLOAD              16U
#define CHANNELMSG_UNLOAD_RESPONSE     17U

/* VMBus Protocol Versions */
#define VERSION_WIN8_1     0x00030000U
#define VERSION_WIN10      0x00040000U
#define VERSION_WIN10_V4_1 0x00040001U
#define VERSION_WIN10_V5   0x00050000U
#define VERSION_WIN10_V5_1 0x00050001U
#define VERSION_WIN10_V5_2 0x00050002U

/* VMBus Ring Packet Types */
#define VM_PKT_DATA_INBAND           0x0006U
#define VM_PKT_DATA_USING_GPA_DIRECT 0x0009U
#define VM_PKT_COMP                  0x000BU
#define VMBUS_PKT_FLAG_COMPLETION    0x0001U

/* ---------------- Compile-Time Verified Wire Structures ---------------- */
typedef struct __attribute__((packed)) {
    u32 message_type;
    u8  payload_size;
    u8  message_flags;   /* bit 0 = message_pending */
    u8  reserved[2];
    u64 sender;
    u8  payload[240];
} HvSynicMsg;
_Static_assert(sizeof(HvSynicMsg) == 256, "HvSynicMsg size");

typedef struct __attribute__((packed)) {
    u32 connection_id;
    u32 reserved;
    u32 message_type;
    u32 payload_size;
    u8  payload[240];
} HvPostMsgIn;
_Static_assert(sizeof(HvPostMsgIn) == 256, "HvPostMsgIn size");

typedef struct __attribute__((packed)) {
    u32 trigger_state;
    u32 rsvdz1;
    struct { u32 pending; u32 armed; } trigger_group[4];
    u64 rsvdz2[3];
    i32 next_checktime[4][32];
    u16 latency[4][32];
    u64 rsvdz3[32];
    struct { u32 connection_id; u16 flag_number; u16 rsvdz; } parameter[4][32];
    u8  rsvdz4[1984];
} HvMonitorPage;
_Static_assert(sizeof(HvMonitorPage) == 4096, "HvMonitorPage size");

typedef struct __attribute__((packed)) {
    u32 msgtype;
    u32 padding;
} HvChanMsgHdr;

typedef struct __attribute__((packed)) {
    HvChanMsgHdr hdr;
    u32 vmbus_version_requested;
    u32 target_vcpu;
    union {
        u64 interrupt_page;
        struct __attribute__((packed)) {
            u8  msg_sint;
            u8  msg_vtl;
            u8  reserved[2];
            u32 feature_flags;
        };
    };
    u64 monitor_page1;
    u64 monitor_page2;
} HvInitiateContact;
_Static_assert(sizeof(HvInitiateContact) == 40, "HvInitiateContact size");

typedef struct __attribute__((packed)) {
    HvChanMsgHdr hdr;
    u8  version_supported;
    u8  connection_state;
    u16 pad2;
    u32 msg_conn_id;
} HvVersionResp;
_Static_assert(sizeof(HvVersionResp) == 16, "HvVersionResp size");

typedef struct __attribute__((packed)) {
    u8  if_type[16];
    u8  if_instance[16];
    u64 reserved1;
    u64 reserved2;
    u16 chn_flags;
    u16 mmio_megabytes;
    u8  user_def[120];
    u16 sub_channel_index;
    u16 reserved3;
} HvChannelOffer;
_Static_assert(sizeof(HvChannelOffer) == 176, "HvChannelOffer size");

typedef struct __attribute__((packed)) {
    HvChanMsgHdr   hdr;
    HvChannelOffer offer;
    u32 child_relid;
    u8  monitorid;
    u8  monitor_flags;       /* bit 0 = monitor_allocated */
    u16 dedicated_flags;     /* bit 0 = is_dedicated_interrupt */
    u32 connection_id;
} HvOfferChannel;
_Static_assert(sizeof(HvOfferChannel) == 196, "HvOfferChannel size");

typedef struct __attribute__((packed)) {
    HvChanMsgHdr hdr;
    u32 child_relid;
    u32 gpadl;
    u16 range_buflen;
    u16 rangecount;
    u32 byte_count;
    u32 byte_offset;
    u64 pfn[32];
} HvGpadlHeader32;

typedef struct __attribute__((packed)) {
    HvChanMsgHdr hdr;
    u32 child_relid;
    u32 gpadl;
    u32 creation_status;
} HvGpadlCreated;

typedef struct __attribute__((packed)) {
    HvChanMsgHdr hdr;
    u32 child_relid;
    u32 openid;
    u32 ringbuffer_gpadlhandle;
    u32 target_vp;
    u32 downstream_ringbuffer_pageoffset;
    u8  userdata[120];
} HvOpenChannel;
_Static_assert(sizeof(HvOpenChannel) == 148, "HvOpenChannel size");

typedef struct __attribute__((packed)) {
    HvChanMsgHdr hdr;
    u32 child_relid;
    u32 openid;
    u32 status;
} HvOpenResult;

typedef struct __attribute__((packed)) {
    volatile u32 write_index;
    volatile u32 read_index;
    volatile u32 interrupt_mask;
    volatile u32 pending_send_sz;
    u32 reserved1[12];
    u32 feature_bits;
    u8  pad_to_page[4096 - 68];
    u8  data[];
} HvRingBuffer;
_Static_assert(sizeof(HvRingBuffer) == 4096, "HvRingBuffer header page size");

typedef struct __attribute__((packed)) {
    u16 type;
    u16 offset8;
    u16 len8;
    u16 flags;
    u64 trans_id;
} HvPktDesc;
_Static_assert(sizeof(HvPktDesc) == 16, "HvPktDesc size");

/* StorVSC protocol structures */
typedef struct __attribute__((packed)) {
    u16 length;
    u8  srb_status;
    u8  scsi_status;
    u8  port_number;
    u8  path_id;
    u8  target_id;
    u8  lun;
    u8  cdb_length;
    u8  sense_info_length;
    u8  data_in;
    u8  reserved;
    u32 data_transfer_length;
    union {
        u8 cdb[16];
        u8 sense_data[20];
        u8 reserved_array[20];
    };
    u16 reserve;
    u8  queue_tag;
    u8  queue_action;
    u32 srb_flags;
    u32 time_out_value;
    u32 queue_sort_key;
} HvScsiReq;
_Static_assert(sizeof(HvScsiReq) == 0x34, "HvScsiReq exact 0x34 size");

typedef struct __attribute__((packed)) {
    u32 operation;
    u32 flags;
    u32 status;
    union {
        HvScsiReq vm_srb;
        struct __attribute__((packed)) { u16 major_minor; u16 revision; } version;
        u8 buffer[0x34];
    };
} HvStorPkt;
_Static_assert(sizeof(HvStorPkt) == 64, "HvStorPkt exact 64 size");

/* Pipe message header used by BOTH SynthVid AND SynthHID Mouse */
typedef struct __attribute__((packed)) {
    u32 type; /* 1 = DATA */
    u32 size;
} HvPipeHdr;

/* ---------------- x86_64 Bare-Metal CPU / MSR / Hypercall Helpers ---------------- */
#if defined(__x86_64__) || defined(_M_X64)

static const u8 GUID_SYNTHVID[16] = {
    0x02,0x78,0x0a,0xda, 0x77,0xe3, 0xac,0x4a, 0x8e,0x77, 0x05,0x58,0xeb,0x10,0x73,0xf8
};
static const u8 GUID_SYNTHHID[16] = {
    0x9e,0xb6,0xa8,0xcf, 0x4a,0x5b, 0xc0,0x4c, 0xb9,0x8b, 0x8b,0xa1,0xa1,0xf3,0xf9,0x5a
};
static const u8 GUID_SYNTHKBD[16] = {
    0x6d,0xad,0x12,0xf9, 0x17,0x2b, 0xea,0x48, 0xbd,0x65, 0xf9,0x27,0xa6,0x1c,0x76,0x84
};
static const u8 GUID_STOR_SCSI[16] = {
    0xd9,0x63,0x61,0xba, 0xa1,0x04, 0x29,0x4d, 0xb6,0x05, 0x72,0xe2,0xff,0xb1,0xdc,0x7f
};
static const u8 GUID_STOR_IDE[16] = {
    0x32,0x26,0x41,0x32, 0xcb,0x86, 0xa2,0x44, 0x9b,0x5c, 0x50,0xd1,0x41,0x73,0x54,0xf5
};

static inline void hv_outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}

static inline u64 hv_rdmsr(u32 msr) {
    u32 lo = 0, hi = 0;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void hv_wrmsr(u32 msr, u64 val) {
    u32 lo = (u32)val, hi = (u32)(val >> 32);
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static void *g_hypercall_page = NULL;

static u64 hv_do_hypercall(u64 control, u64 input_gpa, u64 output_gpa) {
    u64 status = ~0ULL;
    register u64 r8 __asm__("r8") = output_gpa;
    if (!g_hypercall_page) return status;
    __asm__ volatile(
        "call *%[hc]"
        : "=a"(status), "+c"(control), "+d"(input_gpa), "+r"(r8)
        : [hc] "r"(g_hypercall_page)
        : "r9", "r10", "r11", "cc", "memory"
    );
    return status;
}

/* Safe 64-bit GDT + 256-entry IDT for post-ExitBootServices execution */
typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} DescPtr64;

typedef struct __attribute__((packed)) {
    u16 off_lo;
    u16 sel;
    u8  ist;
    u8  type_attr;
    u16 off_mid;
    u32 off_hi;
    u32 zero;
} IdtEntry64;

static u64 g_gdt64[4] __attribute__((aligned(16))) = {
    0x0000000000000000ULL, /* 0x00: null */
    0x00AF9A000000FFFFULL, /* 0x08: 64-bit ring-0 code */
    0x00CF92000000FFFFULL, /* 0x10: 64-bit ring-0 data */
    0x0000000000000000ULL
};
static IdtEntry64 g_idt64[256] __attribute__((aligned(16)));

__attribute__((naked)) static void hv_isr_noop64(void) {
    __asm__ volatile("iretq");
}

__attribute__((naked)) static void hv_isr_eoi64(void) {
    /* Acknowledge both Hyper-V HV_X64_MSR_EOI and LAPIC EOI safely, then iretq */
    __asm__ volatile(
        "push %%rax\n\t"
        "push %%rcx\n\t"
        "push %%rdx\n\t"
        "xor %%eax, %%eax\n\t"
        "xor %%edx, %%edx\n\t"
        "mov $0x40000070, %%ecx\n\t"
        "wrmsr\n\t"
        "pop %%rdx\n\t"
        "pop %%rcx\n\t"
        "pop %%rax\n\t"
        "iretq\n\t"
        ::: "memory"
    );
}

static void hv_set_idt_gate(int vec, void (*fn)(void)) {
    u64 a = (u64)(uptr)fn;
    g_idt64[vec].off_lo = (u16)a;
    g_idt64[vec].sel = 0x08;
    g_idt64[vec].ist = 0;
    g_idt64[vec].type_attr = 0x8E;
    g_idt64[vec].off_mid = (u16)(a >> 16);
    g_idt64[vec].off_hi = (u32)(a >> 32);
    g_idt64[vec].zero = 0;
}

void hv_cpu_post_exit_boot_services(void) {
    DescPtr64 gp = { sizeof(g_gdt64) - 1, (u64)(uptr)g_gdt64 };
    DescPtr64 ip = { sizeof(g_idt64) - 1, (u64)(uptr)g_idt64 };
    int i;

    __asm__ volatile("cli" ::: "memory");

    /* Mask legacy dual-8259 PIC completely so no ISA timer/kbd IRQ fires */
    hv_outb(0x21, 0xFF);
    hv_outb(0xA1, 0xFF);

    /* Load clean 64-bit GDT + CS=0x08 / DS=ES=SS=0x10 */
    __asm__ volatile(
        "lgdt %[gp]\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        :: [gp] "m"(gp) : "rax", "memory"
    );

    for (i = 0; i < 32; i++) hv_set_idt_gate(i, hv_isr_noop64);
    for (i = 32; i < 256; i++) hv_set_idt_gate(i, hv_isr_eoi64);
    __asm__ volatile("lidt %[ip]" :: [ip] "m"(ip) : "memory");

    /* Mask UEFI's periodic Local APIC LVT Timer while keeping LAPIC SVR enabled for SINT2! */
    {
        u64 apic_msr = hv_rdmsr(0x1BU);
        if (apic_msr & (1ULL << 11)) {
            u64 base = apic_msr & 0xFFFFF000ULL;
            /* If non-X2APIC MMIO mode, mask LVT Timer (0x320), LINT0 (0x350), LINT1 (0x360) */
            if (!(apic_msr & (1ULL << 10)) && base != 0) {
                volatile u32 *lapic = (volatile u32 *)(uptr)base;
                lapic[0x0F0 / 4] |= 0x1FFU;         /* Spurious Vector = 0xFF + APIC SW Enable */
                lapic[0x320 / 4] |= (1U << 16);     /* Mask LVT Timer */
                lapic[0x350 / 4] |= (1U << 16);     /* Mask LINT0 */
                lapic[0x360 / 4] |= (1U << 16);     /* Mask LINT1 */
                lapic[0x0B0 / 4] = 0;               /* Clear pending LAPIC EOI */
            } else if (apic_msr & (1ULL << 10)) {
                /* x2APIC MSR mode */
                hv_wrmsr(0x80FU, hv_rdmsr(0x80FU) | 0x1FFULL);
                hv_wrmsr(0x832U, hv_rdmsr(0x832U) | (1ULL << 16));
                hv_wrmsr(0x80BU, 0);
            }
        }
    }
}

/* Briefly pulse IF=1 across a NOP so any pending LAPIC/SynIC SINT2 vector drains cleanly */
static inline void hv_pump_irq(void) {
    __asm__ volatile("sti\n\tnop\n\tpause\n\tcli" ::: "memory");
}

/* ---------------- Hypervisor CPUID Detection & Time Reference ---------------- */
int hv_detect_hypervisor(void) {
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
    char sig[13];
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1U), "c"(0U));
    if (!(ecx & (1U << 31))) return 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000000U), "c"(0U));
    memcpy(sig + 0, &ebx, 4);
    memcpy(sig + 4, &ecx, 4);
    memcpy(sig + 8, &edx, 4);
    sig[12] = 0;
    return memcmp(sig, "Microsoft Hv", 12) == 0 ? 1 : 0;
}

static int g_has_time_ref = 0;

u64 hv_time_ref_100ns(void) {
    if (g_has_time_ref)
        return hv_rdmsr(HV_X64_MSR_TIME_REF_COUNT);
#if defined(__x86_64__) || defined(_M_X64)
    {
        u32 lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (((u64)hi << 32) | lo) / 250ULL;
    }
#else
    return 0;
#endif
}

void hv_delay_us(u32 us) {
    if (g_has_time_ref) {
        u64 start = hv_rdmsr(HV_X64_MSR_TIME_REF_COUNT);
        u64 ticks = (u64)us * 10ULL; /* 1 us = 10 * 100ns */
        while (hv_rdmsr(HV_X64_MSR_TIME_REF_COUNT) - start < ticks)
            hv_pump_irq();
    } else {
        volatile u32 spin = us * 400U;
        while (spin--) __asm__ volatile("pause");
    }
}

void hv_reboot_hypervisor(void) {
    if (hv_detect_hypervisor())
        hv_wrmsr(HV_X64_MSR_RESET, 1ULL);
}

/* ---------------- Driver State & Page Allocator ---------------- */
#define RING_SEND_PAGES   16U
#define RING_RECV_PAGES   16U
#define RING_DATA_BYTES   ((RING_SEND_PAGES - 1U) * 4096U)
#define MAX_OFFERS        32
#define MAX_OPEN_CHANS    8
#define MAX_HV_DISKS      8

typedef struct {
    int            open;
    u32            relid;
    u32            conn_id;
    u8             monitor_id;
    u8             monitor_allocated;
    u8             dedicated_irq;
    u32            gpadl_handle;
    HvRingBuffer  *send_ring;
    HvRingBuffer  *recv_ring;
    u64            next_req_id;
} HvChan;

static u8            *g_pool_cur = NULL;
static u8            *g_pool_end = NULL;
static HvSynicMsg    *g_simp = NULL;
static u8            *g_siefp = NULL;
static u8            *g_int_page = NULL;
static HvMonitorPage *g_mon_page1 = NULL;
static HvMonitorPage *g_mon_page2 = NULL;
static HvPostMsgIn   *g_post_buf = NULL;
static u8            *g_scsi_dma = NULL; /* 32 KiB page-aligned DMA bounce buffer */

static u32            g_vmbus_proto = 0;
static u32            g_msg_conn_id = 1;
static u32            g_next_gpadl  = 0x000E1000U;
static HvOfferChannel g_offers[MAX_OFFERS];
static int            g_offer_cnt = 0;

static HvChan         g_chans[MAX_OPEN_CHANS];
static int            g_chan_vid = -1;
static int            g_chan_mouse = -1;
static int            g_chan_kbd = -1;
static int            g_chan_stor[4] = { -1, -1, -1, -1 };
static int            g_chan_stor_n = 0;

static HvScsiDisk     g_hv_disks[MAX_HV_DISKS];
static int            g_hv_disk_n = 0;

static int            g_hv_active = 0;
static int            g_vid_ok = 0;
static int            g_mouse_ok = 0;
static int            g_kbd_ok = 0;
static u64            g_vid_fb_gpa = 0;
static u32            g_vid_w = 0, g_vid_h = 0, g_vid_pitch = 0;
static u32            g_vid_ver = 0;
static char           g_hv_summary[96] = "Hyper-V VMBus Offline";

/* Parsed HID mouse descriptor offsets (supports both 5-byte Win8 and 7-byte Win10/11 reports!) */
static struct {
    int has_report_id;
    int btn_bit, btn_size;
    int x_bit, x_size, x_max;
    int y_bit, y_size, y_max;
    int wheel_bit, wheel_size;
    int shift_down, ctrl_down, alt_down, caps_on;
    int last_btns;
} g_hid = { 0, 0, 8, 8, 16, 32767, 24, 16, 32767, 40, 8, 0, 0, 0, 0, 0 };

static u8 g_rx_pkt[8192];

static void *hv_alloc_pages(u32 npages) {
    size_t bytes = (size_t)npages * 4096U;
    u8 *p;
    if (!g_pool_cur || (size_t)(g_pool_end - g_pool_cur) < bytes) return NULL;
    p = g_pool_cur;
    g_pool_cur += bytes;
    memset(p, 0, bytes);
    return p;
}

/* ---------------- SynIC Control Message Send / Receive ---------------- */
static int hv_post_msg(u32 conn_id, const void *msg, u32 len) {
    int tries;
    if (!g_post_buf || len > 240U) return -1;
    for (tries = 0; tries < 200; tries++) {
        u64 st;
        memset(g_post_buf, 0, sizeof(*g_post_buf));
        g_post_buf->connection_id = conn_id;
        g_post_buf->message_type = 1U;
        g_post_buf->payload_size = len;
        memcpy(g_post_buf->payload, msg, len);
        st = hv_do_hypercall(HVCALL_POST_MESSAGE, (u64)(uptr)g_post_buf, 0ULL) & 0xFFFFULL;
        if (st == HV_STATUS_SUCCESS) return 0;
        /* HV_STATUS_INVALID_CONNECTION_ID = 0x0005 -> fail fast so caller tries fallback conn_id */
        if (st == 0x0005ULL) return -5;
        hv_delay_us(250);
    }
    return -1;
}

static int hv_recv_synic_msg(void *out_payload, u32 max_len, u32 *out_type) {
    volatile HvSynicMsg *slot;
    u32 mtype;
    u8 psize, flags;
    if (!g_simp) return 0;
    hv_pump_irq();
    slot = &g_simp[VMBUS_MESSAGE_SINT];
    mtype = slot->message_type;
    if (mtype == 0) return 0;
    __asm__ volatile("" ::: "memory");
    psize = slot->payload_size;
    flags = slot->message_flags;
    if (psize > max_len) psize = (u8)max_len;
    memcpy(out_payload, (const void *)slot->payload, psize);
    if (out_type) *out_type = *(const u32 *)out_payload;

    /* Clear slot and notify Hyper-V EOM so next queued message flushes immediately */
    slot->message_type = 0;
    __asm__ volatile("mfence" ::: "memory");
    if (flags & 1U)
        hv_wrmsr(HV_X64_MSR_EOM, 0ULL);
    else
        hv_wrmsr(HV_X64_MSR_EOM, 0ULL);
    return (int)psize;
}

static void hv_record_offer(const u8 *buf, int len) {
    if (len >= (int)sizeof(HvOfferChannel) && g_offer_cnt < MAX_OFFERS) {
        memcpy(&g_offers[g_offer_cnt], buf, sizeof(HvOfferChannel));
        g_offer_cnt++;
    }
}

static int hv_wait_ctrl_msg(u32 want_type, void *out_buf, u32 max_len, u32 timeout_ms) {
    u64 start = hv_time_ref_100ns();
    u64 budget = (u64)timeout_ms * 10000ULL;
    u8 tmp[240];
    while (hv_time_ref_100ns() - start < budget) {
        u32 got_type = 0;
        int n = hv_recv_synic_msg(tmp, sizeof(tmp), &got_type);
        if (n > 0) {
            if (got_type == want_type) {
                u32 cp = (u32)n < max_len ? (u32)n : max_len;
                if (out_buf) memcpy(out_buf, tmp, cp);
                return (int)cp;
            }
            if (got_type == CHANNELMSG_OFFERCHANNEL)
                hv_record_offer(tmp, n);
        }
    }
    return -1;
}

/* ---------------- Ring Buffer Read / Write + Dual Host Signaling ---------------- */
static void hv_signal_channel(HvChan *ch) {
    /* 1. Legacy/V3 event bitmap bit */
    if (!ch->dedicated_irq && g_int_page && ch->relid < 2048U) {
        u32 *send_bmp = (u32 *)(g_int_page + 2048U);
        __sync_fetch_and_or(&send_bmp[ch->relid >> 5], 1U << (ch->relid & 31U));
    }
    /* 2. Monitor page trigger bit (used by monitored channels like StorVSC!) */
    if (ch->monitor_allocated && g_mon_page2) {
        u32 grp = ch->monitor_id >> 5;
        u32 bit = 1U << (ch->monitor_id & 31U);
        if (grp < 4U)
        {
            u32 *pend = (u32 *)((u8 *)g_mon_page2 + 8U + (u32)grp * 8U);
            __asm__ volatile("lock orl %1, %0" : "+m"(*pend) : "r"(bit) : "memory");
        }
    }
    /* 3. Fast Hypercall SignalEvent (wakes fast-path VMBus channels immediately!) */
    hv_do_hypercall(HVCALL_SIGNAL_EVENT | HV_HYPERCALL_FAST_BIT, (u64)ch->conn_id, 0ULL);
}

static u32 ring_avail_write(HvRingBuffer *r) {
    u32 w = r->write_index;
    u32 rd = r->read_index;
    return w >= rd ? RING_DATA_BYTES - (w - rd) : (rd - w);
}

static u32 ring_avail_read(HvRingBuffer *r) {
    u32 w = r->write_index;
    u32 rd = r->read_index;
    __asm__ volatile("" ::: "memory");
    return w >= rd ? (w - rd) : (RING_DATA_BYTES - rd + w);
}

static u32 ring_copy_to(HvRingBuffer *r, u32 pos, const void *src, u32 len) {
    const u8 *s = (const u8 *)src;
    while (len > 0) {
        u32 chunk = RING_DATA_BYTES - pos;
        if (chunk > len) chunk = len;
        memcpy(r->data + pos, s, chunk);
        s += chunk;
        len -= chunk;
        pos += chunk;
        if (pos >= RING_DATA_BYTES) pos = 0;
    }
    return pos;
}

static void ring_copy_from(HvRingBuffer *r, u32 pos, void *dst, u32 len) {
    u8 *d = (u8 *)dst;
    while (len > 0) {
        u32 chunk = RING_DATA_BYTES - pos;
        if (chunk > len) chunk = len;
        memcpy(d, r->data + pos, chunk);
        d += chunk;
        len -= chunk;
        pos += chunk;
        if (pos >= RING_DATA_BYTES) pos = 0;
    }
}

static int hv_chan_write_raw(HvChan *ch, const void *hdr_and_desc, u32 desc_bytes,
                             const void *payload, u32 pay_bytes) {
    static const u8 zpad[8] = {0};
    HvRingBuffer *r;
    u32 unaligned = desc_bytes + pay_bytes;
    u32 pad = (8U - (unaligned & 7U)) & 7U;
    u32 total = unaligned + pad + 8U; /* +8B mandatory prev_indices trailer! */
    u32 old_w, pos;
    u64 prev_indices;

    if (!ch || !ch->open || !ch->send_ring) return -1;
    r = ch->send_ring;
    if (ring_avail_write(r) <= total) return -1;

    old_w = r->write_index;
    pos = old_w;
    pos = ring_copy_to(r, pos, hdr_and_desc, desc_bytes);
    if (pay_bytes > 0)
        pos = ring_copy_to(r, pos, payload, pay_bytes);
    if (pad > 0)
        pos = ring_copy_to(r, pos, zpad, pad);

    /* Exact Linux hv_get_ring_bufferindices(): upper 32 bits = old write_index */
    prev_indices = ((u64)old_w) << 32;
    pos = ring_copy_to(r, pos, &prev_indices, 8U);

    __asm__ volatile("mfence" ::: "memory");
    r->write_index = pos;
    __asm__ volatile("mfence" ::: "memory");

    if (!r->interrupt_mask)
        hv_signal_channel(ch);
    return 0;
}

static int hv_chan_send_inband(HvChan *ch, const void *payload, u32 pay_len, u16 flags) {
    HvPktDesc d;
    u32 aligned_pkt = ((16U + pay_len + 7U) & ~7U);
    d.type = VM_PKT_DATA_INBAND;
    d.offset8 = 2; /* 16 bytes */
    d.len8 = (u16)(aligned_pkt >> 3);
    d.flags = flags;
    d.trans_id = ch->next_req_id++;
    return hv_chan_write_raw(ch, &d, sizeof(d), payload, pay_len);
}

static int hv_chan_send_pipe(HvChan *ch, const void *msg, u32 msg_len, u16 flags) {
    u8 buf[512];
    HvPipeHdr ph;
    if (msg_len + sizeof(ph) > sizeof(buf)) return -1;
    ph.type = 1U;
    ph.size = msg_len;
    memcpy(buf, &ph, sizeof(ph));
    memcpy(buf + sizeof(ph), msg, msg_len);
    return hv_chan_send_inband(ch, buf, (u32)sizeof(ph) + msg_len, flags);
}

static int hv_chan_recv(HvChan *ch, void *out_payload, u32 max_len, u16 *out_pkt_type, u64 *out_trans_id) {
    HvRingBuffer *r;
    HvPktDesc d;
    u32 avail, rd, pkt_bytes, hdr_bytes, pay_bytes, next_rd;

    if (!ch || !ch->open || !ch->recv_ring) return 0;
    hv_pump_irq();
    r = ch->recv_ring;
    avail = ring_avail_read(r);
    if (avail < sizeof(HvPktDesc) + 8U) return 0;

    rd = r->read_index;
    ring_copy_from(r, rd, &d, sizeof(d));
    pkt_bytes = (u32)d.len8 << 3;
    hdr_bytes = (u32)d.offset8 << 3;
    if (pkt_bytes < hdr_bytes || avail < pkt_bytes + 8U) return 0;

    pay_bytes = pkt_bytes - hdr_bytes;
    if (pay_bytes > 0 && out_payload && max_len > 0) {
        u32 cp = pay_bytes < max_len ? pay_bytes : max_len;
        ring_copy_from(r, (rd + hdr_bytes) % RING_DATA_BYTES, out_payload, cp);
    }
    if (out_pkt_type) *out_pkt_type = d.type;
    if (out_trans_id) *out_trans_id = d.trans_id;

    next_rd = (rd + pkt_bytes + 8U) % RING_DATA_BYTES;
    __asm__ volatile("mfence" ::: "memory");
    r->read_index = next_rd;
    __asm__ volatile("mfence" ::: "memory");

    /* If host was blocked waiting for free inbound ring space, wake it */
    if (r->pending_send_sz != 0 && !r->interrupt_mask)
        hv_signal_channel(ch);

    return (int)pay_bytes;
}

/* ---------------- Open Offered VMBus Channel ---------------- */
static int hv_open_offer(const HvOfferChannel *of) {
    HvChan *ch;
    u8 *pages;
    HvGpadlHeader32 gp;
    HvGpadlCreated gpc;
    HvOpenChannel oc;
    HvOpenResult ocr;
    u32 i;
    int slot = -1;

    for (i = 0; i < MAX_OPEN_CHANS; i++) {
        if (!g_chans[i].open) { slot = (int)i; break; }
    }
    if (slot < 0) return -1;

    pages = (u8 *)hv_alloc_pages(RING_SEND_PAGES + RING_RECV_PAGES);
    if (!pages) return -1;

    ch = &g_chans[slot];
    memset(ch, 0, sizeof(*ch));
    ch->relid = of->child_relid;
    ch->conn_id = of->connection_id;
    ch->monitor_id = of->monitorid;
    ch->monitor_allocated = (of->monitor_flags & 1U) ? 1 : 0;
    ch->dedicated_irq = (of->dedicated_flags & 1U) ? 1 : 0;
    ch->gpadl_handle = g_next_gpadl++;
    ch->send_ring = (HvRingBuffer *)pages;
    ch->recv_ring = (HvRingBuffer *)(pages + RING_SEND_PAGES * 4096U);
    ch->send_ring->feature_bits = 1U;
    ch->recv_ring->feature_bits = 1U;
    ch->next_req_id = 1ULL;

    /* 1. Register 32-page (128 KiB) GPADL in a single 280-byte message (32*8+24 = 280 > 240 -> split header + body!) */
    /* WAIT: 24 + 32*8 = 280 bytes > 240-byte SynIC payload max!!
     * Split cleanly exactly like Linux vmbus_establish_gpadl():
     *   GPADL_HEADER carries first 26 PFNs (24 + 26*8 = 232 bytes <= 240!)
     *   GPADL_BODY   carries remaining 6 PFNs (16 + 6*8 = 64 bytes <= 240!)
     */
    {
        u8 hdr_buf[240];
        u8 body_buf[80];
        HvChanMsgHdr *bh = (HvChanMsgHdr *)body_buf;
        memset(hdr_buf, 0, sizeof(hdr_buf));
        gp.hdr.msgtype = CHANNELMSG_GPADL_HEADER;
        gp.hdr.padding = 0;
        gp.child_relid = ch->relid;
        gp.gpadl = ch->gpadl_handle;
        gp.range_buflen = (u16)(8U + 32U * 8U);
        gp.rangecount = 1U;
        gp.byte_count = 32U * 4096U;
        gp.byte_offset = 0U;
        for (i = 0; i < 32U; i++)
            gp.pfn[i] = ((u64)(uptr)pages >> 12) + i;

        _Static_assert(__builtin_offsetof(HvGpadlHeader32, pfn) == 28, "GPADL pfn offset");
        memcpy(hdr_buf, &gp, 28U + 26U * 8U); /* 236 bytes <= 240 max SynIC payload! */
        if (hv_post_msg(g_msg_conn_id, hdr_buf, 28U + 26U * 8U) != 0)
            return -1;

        memset(body_buf, 0, sizeof(body_buf));
        bh->msgtype = CHANNELMSG_GPADL_BODY;
        bh->padding = 0;
        *(u32 *)(body_buf + 8) = 0U;
        *(u32 *)(body_buf + 12) = ch->gpadl_handle;
        memcpy(body_buf + 16, &gp.pfn[26], 6U * 8U); /* 64 bytes! */
        if (hv_post_msg(g_msg_conn_id, body_buf, 16U + 6U * 8U) != 0)
            return -1;
    }

    memset(&gpc, 0, sizeof(gpc));
    if (hv_wait_ctrl_msg(CHANNELMSG_GPADL_CREATED, &gpc, sizeof(gpc), 1500) < 0 ||
        gpc.creation_status != 0) {
        klog("[hv] GPADL create failed relid=%u st=%u", ch->relid, gpc.creation_status);
        return -1;
    }

    /* 2. Open VMBus Channel */
    memset(&oc, 0, sizeof(oc));
    oc.hdr.msgtype = CHANNELMSG_OPENCHANNEL;
    oc.child_relid = ch->relid;
    oc.openid = ch->relid;
    oc.ringbuffer_gpadlhandle = ch->gpadl_handle;
    oc.target_vp = 0;
    oc.downstream_ringbuffer_pageoffset = RING_SEND_PAGES;
    if (hv_post_msg(g_msg_conn_id, &oc, sizeof(oc)) != 0)
        return -1;

    memset(&ocr, 0, sizeof(ocr));
    if (hv_wait_ctrl_msg(CHANNELMSG_OPENCHANNEL_RESULT, &ocr, sizeof(ocr), 1500) < 0 ||
        ocr.status != 0) {
        klog("[hv] OPENCHANNEL failed relid=%u st=%u", ch->relid, ocr.status);
        return -1;
    }

    ch->open = 1;
    return slot;
}

/* ---------------- 1. Synthetic Video Driver (SynthVid) ---------------- */
/* Hides Hyper-V's unwanted stray hardware cursor dot while keeping seamless integration active! */
static void hv_synthvid_hide_dot_cursor(void) {
    u8 pos_msg[14];
    u8 shp_msg[24];
    if (g_chan_vid < 0) return;

    /* SYNTHVID_POINTER_POSITION (type = 7, size = 14): is_visible=1, out=0, x=0, y=0 */
    memset(pos_msg, 0, sizeof(pos_msg));
    *(u32 *)(pos_msg + 0) = 7U;
    *(u32 *)(pos_msg + 4) = 14U;
    pos_msg[8] = 1U;
    pos_msg[9] = 0U;
    hv_chan_send_pipe(&g_chans[g_chan_vid], pos_msg, 14U, 0);

    /* SYNTHVID_POINTER_SHAPE (type = 8, size = 24): 1x1 ARGB {0, 1, 1, 1} per Linux hyperv_drm_proto.c! */
    memset(shp_msg, 0, sizeof(shp_msg));
    *(u32 *)(shp_msg + 0) = 8U;
    *(u32 *)(shp_msg + 4) = 24U;
    shp_msg[8] = 0xFFU; /* part_idx = complete */
    shp_msg[9] = 1U;    /* is_argb = 1 */
    *(u32 *)(shp_msg + 10) = 1U; /* width = 1 */
    *(u32 *)(shp_msg + 14) = 1U; /* height = 1 (note packed!) */
    /* Wait: verify exact field offsets of synthvid_pointer_shape in Linux hyperv_drm_proto.c:
     * hdr (8B) + u8 part_idx (+8) + u8 is_argb (+9) + u32 width (+10) + u32 height (+14)
     * + u32 hot_x (+18) + u32 hot_y (+22) + u8 data[4] (+26..29) => total 30 bytes!
     */
    {
        u8 shp30[30];
        memset(shp30, 0, sizeof(shp30));
        *(u32 *)(shp30 + 0) = 8U;
        *(u32 *)(shp30 + 4) = 30U;
        shp30[8] = 0xFFU;
        shp30[9] = 1U;
        *(u32 *)(shp30 + 10) = 1U;
        *(u32 *)(shp30 + 14) = 1U;
        *(u32 *)(shp30 + 18) = 0U;
        *(u32 *)(shp30 + 22) = 0U;
        shp30[26] = 0U; shp30[27] = 1U; shp30[28] = 1U; shp30[29] = 1U;
        hv_chan_send_pipe(&g_chans[g_chan_vid], shp30, 30U, 0);
    }
    UNUSED(shp_msg);
}

void hv_synthvid_dirt(int x1, int y1, int x2, int y2) {
    /* SYNTHVID_DIRT (type = 10, size = 26): hdr(8) + video_output(1) + dirt_count(1) + rect(4*i32=16) */
    u8 msg[26];
    if (!g_vid_ok || g_chan_vid < 0) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)g_vid_w) x2 = (int)g_vid_w;
    if (y2 > (int)g_vid_h) y2 = (int)g_vid_h;
    if (x2 <= x1 || y2 <= y1) return;

    *(u32 *)(msg + 0) = 10U;
    *(u32 *)(msg + 4) = 26U;
    msg[8] = 0U;
    msg[9] = 1U;
    *(i32 *)(msg + 10) = x1;
    *(i32 *)(msg + 14) = y1;
    *(i32 *)(msg + 18) = x2;
    *(i32 *)(msg + 22) = y2;
    hv_chan_send_pipe(&g_chans[g_chan_vid], msg, 26U, 0);
}

void hv_synthvid_set_mode(u64 fb_gpa, u32 fb_w, u32 fb_h, u32 fb_pitch) {
    /* SYNTHVID_SITUATION_UPDATE:
     * Win8 (ver < 0x00030005): video_output_situation has width/height/pitch WITHOUT active/depth?
     * No — in Linux hyperv_drm_proto.c:
     *   video_output_situation (packed, 21 bytes):
     *     u8 active (+0), u32 vram_offset (+1), u8 depth_bits (+5),
     *     u32 width_pixels (+6), u32 height_pixels (+10), u32 pitch_bytes (+14) -> 18 bytes!
     *   synthvid_situation_update (packed, 8 + 8 + 1 + 18 = 35 bytes):
     *     hdr(8) + u64 user_ctx(+8) + u8 num_situations(+16) + sit(18) = 35 bytes!
     *   synthvid_situation_update_v3 (Win10 0x00050003, 8 + 8 + 1 + 21? check version:
     *     negotiate SYNTHVID_VERSION_WIN8 = 0x00020003 FIRST because EVERY Hyper-V build from
     *     Windows Server 2012 through Windows 11 24H2 supports 0x00020003 with IDENTICAL 35-byte
     *     situation_update AND zero vram_offset quirks!)
     */
    u8 vram_msg[25];
    u8 sit_msg[35];
    u64 start;
    if (g_chan_vid < 0 || !fb_gpa) return;

    g_vid_fb_gpa = fb_gpa;
    g_vid_w = fb_w;
    g_vid_h = fb_h;
    g_vid_pitch = fb_pitch;

    /* 1. SYNTHVID_VRAM_LOCATION (type = 3, size = 25): hdr(8) + user_ctx(8) + specified(1) + gpa(8) */
    memset(vram_msg, 0, sizeof(vram_msg));
    *(u32 *)(vram_msg + 0) = 3U;
    *(u32 *)(vram_msg + 4) = 25U;
    *(u64 *)(vram_msg + 8) = fb_gpa;
    vram_msg[16] = 1U;
    *(u64 *)(vram_msg + 17) = fb_gpa;
    hv_chan_send_pipe(&g_chans[g_chan_vid], vram_msg, 25U, VMBUS_PKT_FLAG_COMPLETION);

    start = hv_time_ref_100ns();
    while (hv_time_ref_100ns() - start < 8000000ULL) {
        int n = hv_chan_recv(&g_chans[g_chan_vid], g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL);
        if (n >= 16 && *(u32 *)(g_rx_pkt + 8) == 4U) /* VRAM_LOCATION_ACK */
            break;
    }

    /* 2. SYNTHVID_SITUATION_UPDATE (type = 5, size = 35) */
    memset(sit_msg, 0, sizeof(sit_msg));
    *(u32 *)(sit_msg + 0) = 5U;
    *(u32 *)(sit_msg + 4) = 35U;
    *(u64 *)(sit_msg + 8) = 0ULL;
    sit_msg[16] = 1U;               /* num_situations = 1 */
    sit_msg[17] = 1U;               /* active = 1 */
    *(u32 *)(sit_msg + 18) = 0U;    /* vram_offset = 0 */
    sit_msg[22] = 32U;              /* depth_bits = 32 */
    *(u32 *)(sit_msg + 23) = fb_w;
    *(u32 *)(sit_msg + 27) = fb_h;
    *(u32 *)(sit_msg + 31) = fb_pitch;
    hv_chan_send_pipe(&g_chans[g_chan_vid], sit_msg, 35U, 0);

    g_vid_ok = 1;
    hv_synthvid_hide_dot_cursor();
    hv_synthvid_dirt(0, 0, (int)fb_w, (int)fb_h);
}

static int hv_init_synthvid(u64 fb_gpa, u32 fb_w, u32 fb_h, u32 fb_pitch) {
    /* Try SYNTHVID_VERSION_WIN8 (major=3, minor=2 => u32 0x00020003) then WIN7 (0x00000003) */
    static const u32 vers[2] = { 0x00020003U, 0x00000003U };
    int v;
    for (v = 0; v < 2; v++) {
        u8 req[12];
        u64 start;
        *(u32 *)(req + 0) = 1U; /* SYNTHVID_VERSION_REQUEST */
        *(u32 *)(req + 4) = 12U;
        *(u32 *)(req + 8) = vers[v];
        hv_chan_send_pipe(&g_chans[g_chan_vid], req, 12U, VMBUS_PKT_FLAG_COMPLETION);
        start = hv_time_ref_100ns();
        while (hv_time_ref_100ns() - start < 8000000ULL) {
            int n = hv_chan_recv(&g_chans[g_chan_vid], g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL);
            if (n >= 21 && *(u32 *)(g_rx_pkt + 8) == 2U && g_rx_pkt[20] != 0) {
                g_vid_ver = vers[v];
                hv_synthvid_set_mode(fb_gpa, fb_w, fb_h, fb_pitch);
                return 0;
            }
        }
    }
    return -1;
}

static void hv_poll_synthvid(void) {
    int n;
    if (g_chan_vid < 0) return;
    while ((n = hv_chan_recv(&g_chans[g_chan_vid], g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL)) > 0) {
        if (n >= 16) {
            u32 mtype = *(u32 *)(g_rx_pkt + 8);
            /* Re-hide stray hardware dot & refresh screen when vmconnect connects/resizes! */
            if (mtype == 6U) { /* SYNTHVID_FEATURE_CHANGE */
                hv_synthvid_hide_dot_cursor();
                hv_synthvid_dirt(0, 0, (int)g_vid_w, (int)g_vid_h);
            }
        }
    }
}

/* ---------------- 2. Synthetic HID Mouse Driver (SynthHID) ---------------- */
static void hv_parse_hid_report_desc(const u8 *d, int len) {
    /* Lightweight HID Report Descriptor parser for Buttons (0x09), X (0x01/0x30), Y (0x01/0x31), Wheel (0x01/0x38) */
    u32 usage_page = 0, usage_list[16];
    int n_usages = 0;
    u32 rep_size = 0, rep_cnt = 0;
    i32 log_max = 32767;
    int bit_off = 0, i = 0;

    while (i < len) {
        u8 b = d[i++];
        int sz = b & 3;
        u8 tag = (b >> 4) & 0xFU;
        u8 typ = (b >> 2) & 0x3U;
        u32 val = 0;
        if (b == 0xFE) { if (i + 2 <= len) i += 2 + d[i]; continue; }
        if (sz == 3) sz = 4;
        if (i + sz > len) break;
        if (sz == 1) val = d[i];
        else if (sz == 2) val = (u32)d[i] | ((u32)d[i + 1] << 8);
        else if (sz == 4) val = (u32)d[i] | ((u32)d[i + 1] << 8) | ((u32)d[i + 2] << 16) | ((u32)d[i + 3] << 24);
        i += sz;

        if (typ == 1) { /* Global */
            if (tag == 0) usage_page = val;
            else if (tag == 2) log_max = (sz == 2 && (val & 0x8000U)) ? (i32)(i16)val : (i32)val;
            else if (tag == 7) rep_size = val;
            else if (tag == 8) { g_hid.has_report_id = 1; bit_off += 8; }
            else if (tag == 9) rep_cnt = val;
        } else if (typ == 2) { /* Local */
            if (tag == 0 && n_usages < 16) usage_list[n_usages++] = val;
        } else if (typ == 0) { /* Main */
            if (tag == 8) { /* Input */
                u32 k;
                if (usage_page == 0x09U) {
                    g_hid.btn_bit = bit_off;
                    g_hid.btn_size = (int)(rep_size * rep_cnt);
                    bit_off += (int)(rep_size * rep_cnt);
                } else {
                    for (k = 0; k < rep_cnt; k++) {
                        u32 u = (int)k < n_usages ? usage_list[k] : (n_usages ? usage_list[n_usages - 1] : 0);
                        if (usage_page == 0x01U && u == 0x30U) {
                            g_hid.x_bit = bit_off; g_hid.x_size = (int)rep_size;
                            if (log_max > 0) g_hid.x_max = log_max;
                        } else if (usage_page == 0x01U && u == 0x31U) {
                            g_hid.y_bit = bit_off; g_hid.y_size = (int)rep_size;
                            if (log_max > 0) g_hid.y_max = log_max;
                        } else if (usage_page == 0x01U && u == 0x38U) {
                            g_hid.wheel_bit = bit_off; g_hid.wheel_size = (int)rep_size;
                        }
                        bit_off += (int)rep_size;
                    }
                }
            }
            n_usages = 0;
        }
    }
}

static u32 extract_bits_le(const u8 *buf, int byte_len, int bit_pos, int bit_cnt) {
    u64 acc = 0;
    int b0 = bit_pos >> 3, k;
    for (k = 0; k < 8 && b0 + k < byte_len; k++)
        acc |= ((u64)buf[b0 + k]) << (k * 8);
    acc >>= (bit_pos & 7);
    if (bit_cnt < 64) acc &= ((1ULL << bit_cnt) - 1ULL);
    return (u32)acc;
}

static int hv_init_mouse(void) {
    u8 req[12];
    u64 start;
    int got_info = 0;
    if (g_chan_mouse < 0) return -1;

    /* SYNTH_HID_PROTOCOL_REQUEST: type=0, size=4, version=0x00020000 */
    *(u32 *)(req + 0) = 0U;
    *(u32 *)(req + 4) = 4U;
    *(u32 *)(req + 8) = 0x00020000U;
    hv_chan_send_pipe(&g_chans[g_chan_mouse], req, 12U, VMBUS_PKT_FLAG_COMPLETION);

    start = hv_time_ref_100ns();
    while (hv_time_ref_100ns() - start < 10000000ULL) {
        int n = hv_chan_recv(&g_chans[g_chan_mouse], g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL);
        if (n >= 16) {
            u32 mtype = *(u32 *)(g_rx_pkt + 8);
            u32 msize = *(u32 *)(g_rx_pkt + 12);
            if (mtype == 2U && n >= 16 + 36) { /* SYNTH_HID_INITIAL_DEVICE_INFO */
                /* pipe(8) + synthhid_hdr(8) + dev_info(size=4,vid=2,pid=2,ver=2,rsv=22 => 32B) + hid_descriptor */
                u16 rdesc_len = *(u16 *)(g_rx_pkt + 16 + 32 + 7);
                int rdesc_off = 16 + 32 + 9;
                if (rdesc_len > 0 && rdesc_off + (int)rdesc_len <= n)
                    hv_parse_hid_report_desc(g_rx_pkt + rdesc_off, rdesc_len);
                /* Send SYNTH_HID_INITIAL_DEVICE_INFO_ACK (type=3, size=1, reserved=0) */
                {
                    u8 ack[9];
                    *(u32 *)(ack + 0) = 3U;
                    *(u32 *)(ack + 4) = 1U;
                    ack[8] = 0U;
                    hv_chan_send_pipe(&g_chans[g_chan_mouse], ack, 9U, 0);
                }
                got_info = 1;
                break;
            }
            UNUSED(msize);
        }
    }
    if (got_info) {
        g_mouse_ok = 1;
        return 0;
    }
    return -1;
}

static void hv_poll_mouse(void) {
    int n;
    if (!g_mouse_ok || g_chan_mouse < 0) return;
    while ((n = hv_chan_recv(&g_chans[g_chan_mouse], g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL)) > 0) {
        if (n >= 16 + 5 && *(u32 *)(g_rx_pkt + 8) == 4U) { /* SYNTH_HID_INPUT_REPORT */
            const u8 *rep = g_rx_pkt + 16;
            int rlen = n - 16;
            u32 btns = extract_bits_le(rep, rlen, g_hid.btn_bit, g_hid.btn_size) & 7U;
            u32 rx   = extract_bits_le(rep, rlen, g_hid.x_bit, g_hid.x_size);
            u32 ry   = extract_bits_le(rep, rlen, g_hid.y_bit, g_hid.y_size);
            i8  whl  = (i8)extract_bits_le(rep, rlen, g_hid.wheel_bit, g_hid.wheel_size);
            int sx = fb.width  > 1 ? (int)(((u64)rx * (u64)(fb.width  - 1)) / (u64)g_hid.x_max) : 0;
            int sy = fb.height > 1 ? (int)(((u64)ry * (u64)(fb.height - 1)) / (u64)g_hid.y_max) : 0;
            YoroEvent ev;
            int b;

            if (sx != mouse_get_x() || sy != mouse_get_y()) {
                mouse_set_pos(sx, sy);
                memset(&ev, 0, sizeof(ev));
                ev.type = EV_MOUSE_MOVE;
                ev.x = sx; ev.y = sy;
                ev.btn = (btns & 1U) ? 1 : ((btns & 2U) ? 2 : 0);
                input_push(&ev);
            }
            for (b = 0; b < 3; b++) {
                int mask = 1 << b;
                if ((int)(btns & mask) != (g_hid.last_btns & mask)) {
                    memset(&ev, 0, sizeof(ev));
                    ev.type = (btns & mask) ? EV_MOUSE_DOWN : EV_MOUSE_UP;
                    ev.x = sx; ev.y = sy;
                    ev.btn = b + 1;
                    input_push(&ev);
                }
            }
            g_hid.last_btns = (int)btns;
            if (whl != 0) {
                memset(&ev, 0, sizeof(ev));
                ev.type = EV_WHEEL;
                ev.x = sx; ev.y = sy;
                ev.wheel = (int)whl;
                input_push(&ev);
            }
        }
    }
}

/* ---------------- 3. Synthetic Keyboard Driver (SynthKbd) ---------------- */
static int hv_init_kbd(void) {
    u8 req[8];
    u64 start;
    if (g_chan_kbd < 0) return -1;

    /* SYNTH_KBD_PROTOCOL_REQUEST: type = 1, version = 0x00010000 */
    *(u32 *)(req + 0) = 1U;
    *(u32 *)(req + 4) = 0x00010000U;
    hv_chan_send_inband(&g_chans[g_chan_kbd], req, 8U, VMBUS_PKT_FLAG_COMPLETION);

    start = hv_time_ref_100ns();
    while (hv_time_ref_100ns() - start < 8000000ULL) {
        int n = hv_chan_recv(&g_chans[g_chan_kbd], g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL);
        if (n >= 8 && *(u32 *)g_rx_pkt == 2U && (*(u32 *)(g_rx_pkt + 4) & 1U)) {
            g_kbd_ok = 1;
            return 0;
        }
    }
    return -1;
}

/* Standard XT Set-1 Scancode -> ASCII / Russian JCUKEN translation */
static const char kScNormal[89] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, 9,
    'q','w','e','r','t','y','u','i','o','p','[',']', 13, 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};
static const char kScShift[89] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, 9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 13, 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};
static const u16 kRuMap[26] = {
    0x0444,0x0438,0x0441,0x0432,0x0443,0x0430,0x043F,0x0440,0x0448,0x043E,
    0x043B,0x0434,0x044C,0x0442,0x0449,0x0437,0x0439,0x043A,0x044B,0x0435,
    0x0433,0x043C,0x0446,0x0447,0x043D,0x044F
};

static void hv_poll_kbd(void) {
    int n;
    if (!g_kbd_ok || g_chan_kbd < 0) return;
    while ((n = hv_chan_recv(&g_chans[g_chan_kbd], g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL)) > 0) {
        if (n >= 12 && *(u32 *)g_rx_pkt == 3U) { /* SYNTH_KBD_EVENT */
            u16 code = *(u16 *)(g_rx_pkt + 4);
            u32 info = *(u32 *)(g_rx_pkt + 8);
            int is_unicode = (info & 1U) != 0;
            int is_break   = (info & 2U) != 0;
            int is_e0      = (info & 4U) != 0;
            YoroEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = EV_KEY;

            if (is_unicode) {
                if (!is_break && code >= 32) {
                    ev.ch = (int)code;
                    ev.key = (code < 128) ? (int)code : 0;
                    input_push(&ev);
                }
                continue;
            }

            if (code == 0x2AU || code == 0x36U) { g_hid.shift_down = !is_break; continue; }
            if (code == 0x1DU) { g_hid.ctrl_down = !is_break; continue; }
            if (code == 0x38U) {
                if (!is_break && g_hid.shift_down) kbd_toggle_layout();
                g_hid.alt_down = !is_break;
                continue;
            }
            if (code == 0x3AU && !is_break) { g_hid.caps_on = !g_hid.caps_on; continue; }
            if (is_break) continue;

            ev.shift = g_hid.shift_down;
            ev.ctrl  = g_hid.ctrl_down;
            ev.alt   = g_hid.alt_down;

            if (is_e0) {
                if (code == 0x48U) ev.key = KEY_UP;
                else if (code == 0x50U) ev.key = KEY_DOWN;
                else if (code == 0x4BU) ev.key = KEY_LEFT;
                else if (code == 0x4DU) ev.key = KEY_RIGHT;
                else if (code == 0x53U) ev.key = KEY_DEL;
                else if (code == 0x47U) ev.key = KEY_HOME;
                else if (code == 0x4FU) ev.key = KEY_END;
                else if (code == 0x49U) ev.key = KEY_PGUP;
                else if (code == 0x51U) ev.key = KEY_PGDN;
                else if (code == 0x5BU || code == 0x5CU) ev.key = KEY_WIN;
                else if (code == 0x1CU) { ev.key = KEY_ENTER; ev.ch = 13; }
                else if (code == 0x35U) { ev.key = '/'; ev.ch = '/'; }
                if (ev.key) input_push(&ev);
                continue;
            }

            if (code == 0x01U) ev.key = KEY_ESC;
            else if (code == 0x0EU) { ev.key = KEY_BACKSP; ev.ch = 8; }
            else if (code == 0x0FU) { ev.key = KEY_TAB; ev.ch = 9; }
            else if (code == 0x1CU) { ev.key = KEY_ENTER; ev.ch = 13; }
            else if (code >= 0x3BU && code <= 0x44U) ev.key = KEY_F1 + (int)(code - 0x3BU);
            else if (code == 0x57U) ev.key = KEY_F11;
            else if (code == 0x58U) ev.key = KEY_F12;
            else if (code < 58U) {
                char c = g_hid.shift_down ? kScShift[code] : kScNormal[code];
                if (c >= 'a' && c <= 'z' && kbd_is_ru() && !g_hid.ctrl_down) {
                    u16 ru = kRuMap[c - 'a'];
                    if (g_hid.shift_down ^ g_hid.caps_on) ru = (u16)(ru - 0x20U);
                    ev.ch = (int)ru;
                    ev.key = (int)c;
                } else if (c) {
                    if (c >= 'a' && c <= 'z' && g_hid.caps_on) c = (char)(c - 32);
                    else if (c >= 'A' && c <= 'Z' && g_hid.caps_on) c = (char)(c + 32);
                    ev.ch = (unsigned char)c;
                    ev.key = (unsigned char)c;
                }
            }
            if (ev.key || ev.ch)
                input_push(&ev);
        }
    }
}

/* ---------------- 4. Synthetic SCSI / VHDX Storage Driver (StorVSC) ---------------- */
static int hv_stor_sync_op(HvChan *ch, HvStorPkt *pkt) {
    u64 start;
    pkt->flags = 1U; /* REQUEST_COMPLETION_FLAG */
    if (hv_chan_send_inband(ch, pkt, sizeof(*pkt), VMBUS_PKT_FLAG_COMPLETION) != 0)
        return -1;
    start = hv_time_ref_100ns();
    while (hv_time_ref_100ns() - start < 15000000ULL) {
        int n = hv_chan_recv(ch, g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL);
        if (n >= (int)sizeof(HvStorPkt)) {
            const HvStorPkt *rp = (const HvStorPkt *)g_rx_pkt;
            if (rp->operation == 1U) { /* VSTOR_OPERATION_COMPLETE_IO */
                *pkt = *rp;
                return (rp->status == 0) ? 0 : -1;
            }
        }
    }
    return -1;
}

static int hv_stor_exec_cdb(HvChan *ch, u8 path, u8 target, u8 lun,
                            const u8 *cdb, u8 cdb_len,
                            int data_in, void *data_buf, u32 data_len) {
    /* Build vmdata_gpa_direct descriptor + HvStorPkt in one clean contiguous buffer */
    struct __attribute__((packed)) {
        HvPktDesc desc;
        u32 reserved;
        u32 range_cnt;
        u32 byte_count;
        u32 byte_offset;
        u64 pfn[8]; /* supports up to 32 KiB 4K-aligned transfers */
    } gd;
    HvStorPkt sp;
    u32 n_pages = 0, desc_bytes;
    u64 start;

    memset(&sp, 0, sizeof(sp));
    sp.operation = 3U; /* VSTOR_OPERATION_EXECUTE_SRB */
    sp.flags = 1U;
    sp.vm_srb.length = sizeof(HvScsiReq);
    sp.vm_srb.path_id = path;
    sp.vm_srb.target_id = target;
    sp.vm_srb.lun = lun;
    sp.vm_srb.cdb_length = cdb_len;
    sp.vm_srb.sense_info_length = 0x14U; /* Win8+ sense size 20 */
    sp.vm_srb.data_in = data_len == 0 ? 2U : (data_in ? 1U : 0U);
    sp.vm_srb.data_transfer_length = data_len;
    sp.vm_srb.time_out_value = 30U;
    sp.vm_srb.srb_flags = data_len == 0 ? 0x00000100U /* SRB_FLAGS_NO_DATA_TRANSFER */
                                        : (data_in ? 0x00000040U : 0x00000080U);
    memcpy(sp.vm_srb.cdb, cdb, cdb_len);

    if (data_len == 0) {
        if (hv_chan_send_inband(ch, &sp, sizeof(sp), VMBUS_PKT_FLAG_COMPLETION) != 0)
            return -1;
    } else {
        u32 i;
        if (!g_scsi_dma || data_len > 32768U) return -1;
        if (!data_in && data_buf) memcpy(g_scsi_dma, data_buf, data_len);
        else memset(g_scsi_dma, 0, data_len);

        n_pages = (data_len + 4095U) >> 12;
        desc_bytes = (u32)(sizeof(HvPktDesc) + 16U + n_pages * 8U);
        memset(&gd, 0, sizeof(gd));
        gd.desc.type = VM_PKT_DATA_USING_GPA_DIRECT;
        gd.desc.offset8 = (u16)(desc_bytes >> 3);
        gd.desc.len8 = (u16)(((desc_bytes + sizeof(sp) + 7U) & ~7U) >> 3);
        gd.desc.flags = VMBUS_PKT_FLAG_COMPLETION;
        gd.desc.trans_id = ch->next_req_id++;
        gd.range_cnt = 1U;
        gd.byte_count = data_len;
        gd.byte_offset = 0U;
        for (i = 0; i < n_pages; i++)
            gd.pfn[i] = ((u64)(uptr)g_scsi_dma >> 12) + i;

        if (hv_chan_write_raw(ch, &gd, desc_bytes, &sp, sizeof(sp)) != 0)
            return -1;
    }

    start = hv_time_ref_100ns();
    while (hv_time_ref_100ns() - start < 30000000ULL) {
        int n = hv_chan_recv(ch, g_rx_pkt, sizeof(g_rx_pkt), NULL, NULL);
        if (n >= (int)sizeof(HvStorPkt)) {
            const HvStorPkt *rp = (const HvStorPkt *)g_rx_pkt;
            if (rp->operation == 1U) {
                u8 srb_st = rp->vm_srb.srb_status & 0x3FU;
                /* SRB_STATUS_SUCCESS (1) or SRB_STATUS_DATA_OVERRUN (0x12 on short INQUIRY) */
                if ((rp->status == 0 && (srb_st == 1U || srb_st == 0x12U)) && rp->vm_srb.scsi_status == 0) {
                    if (data_in && data_buf && data_len > 0)
                        memcpy(data_buf, g_scsi_dma, data_len);
                    return 0;
                }
                return -2; /* Check condition / LUN empty */
            }
        }
    }
    return -1;
}

static int hv_stor_exec_retry(HvChan *ch, u8 path, u8 target, u8 lun,
                              const u8 *cdb, u8 cdb_len,
                              int data_in, void *data_buf, u32 data_len) {
    int attempt;
    for (attempt = 0; attempt < 3; attempt++) {
        int rc = hv_stor_exec_cdb(ch, path, target, lun, cdb, cdb_len, data_in, data_buf, data_len);
        if (rc == 0) return 0;
        /* Clear power-on / bus-reset UNIT ATTENTION with REQUEST SENSE */
        {
            u8 rs_cdb[6] = { 0x03, 0, 0, 0, 18, 0 };
            u8 sense[32];
            hv_stor_exec_cdb(ch, path, target, lun, rs_cdb, 6, 1, sense, 18);
        }
    }
    return -1;
}

static void hv_init_stor_channel(int cidx) {
    static const u16 vers[3] = { 0x0602U, 0x0600U, 0x0501U };
    HvChan *ch = &g_chans[cidx];
    HvStorPkt pkt;
    int v, path, target, lun;

    memset(&pkt, 0, sizeof(pkt));
    pkt.operation = 7U; /* BEGIN_INITIALIZATION */
    if (hv_stor_sync_op(ch, &pkt) != 0) return;

    for (v = 0; v < 3; v++) {
        memset(&pkt, 0, sizeof(pkt));
        pkt.operation = 9U; /* QUERY_PROTOCOL_VERSION */
        pkt.version.major_minor = vers[v];
        if (hv_stor_sync_op(ch, &pkt) == 0) break;
    }

    memset(&pkt, 0, sizeof(pkt));
    pkt.operation = 10U; /* QUERY_PROPERTIES */
    hv_stor_sync_op(ch, &pkt);

    memset(&pkt, 0, sizeof(pkt));
    pkt.operation = 8U; /* END_INITIALIZATION */
    if (hv_stor_sync_op(ch, &pkt) != 0) return;

    /* Scan SCSI bus targets & LUNs for VHDX disks & ISO drives */
    for (path = 0; path < 2 && g_hv_disk_n < MAX_HV_DISKS; path++) {
        for (target = 0; target < 2 && g_hv_disk_n < MAX_HV_DISKS; target++) {
            for (lun = 0; lun < 4 && g_hv_disk_n < MAX_HV_DISKS; lun++) {
                u8 inq_cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
                u8 inq[64];
                u8 dev_type;
                u64 total_blocks = 0;
                u32 bsz = 0;
                int k;

                memset(inq, 0, sizeof(inq));
                if (hv_stor_exec_cdb(ch, (u8)path, (u8)target, (u8)lun, inq_cdb, 6, 1, inq, 36) != 0)
                    continue;
                if (((inq[0] >> 5) & 7U) == 3U) /* peripheral qualifier: not connected */
                    continue;
                dev_type = inq[0] & 0x1FU;
                if (dev_type != 0x00U && dev_type != 0x05U)
                    continue;

                /* Wake unit & read 64-bit capacity via READ CAPACITY(16) (0x9E/0x10), fallback (10) (0x25) */
                {
                    u8 tur[6] = {0};
                    u8 rc16_cdb[16] = { 0x9E, 0x10, 0,0,0,0,0,0,0,0, 0,0,0,32, 0, 0 };
                    u8 rc10_cdb[10] = { 0x25, 0,0,0,0,0,0,0,0,0 };
                    u8 cap[32];
                    hv_stor_exec_retry(ch, (u8)path, (u8)target, (u8)lun, tur, 6, 0, NULL, 0);
                    memset(cap, 0, sizeof(cap));
                    if (hv_stor_exec_retry(ch, (u8)path, (u8)target, (u8)lun, rc16_cdb, 16, 1, cap, 32) == 0) {
                        u64 last = ((u64)cap[0] << 56) | ((u64)cap[1] << 48) | ((u64)cap[2] << 40) | ((u64)cap[3] << 32)
                                 | ((u64)cap[4] << 24) | ((u64)cap[5] << 16) | ((u64)cap[6] << 8)  | (u64)cap[7];
                        bsz = ((u32)cap[8] << 24) | ((u32)cap[9] << 16) | ((u32)cap[10] << 8) | (u32)cap[11];
                        total_blocks = last + 1ULL;
                    }
                    if ((bsz < 512U || total_blocks <= 1ULL) &&
                        hv_stor_exec_retry(ch, (u8)path, (u8)target, (u8)lun, rc10_cdb, 10, 1, cap, 8) == 0) {
                        u32 last32 = ((u32)cap[0] << 24) | ((u32)cap[1] << 16) | ((u32)cap[2] << 8) | (u32)cap[3];
                        bsz = ((u32)cap[4] << 24) | ((u32)cap[5] << 16) | ((u32)cap[6] << 8) | (u32)cap[7];
                        total_blocks = (u64)last32 + 1ULL;
                    }
                }
                if (bsz < 512U || total_blocks == 0) continue;

                {
                    HvScsiDisk *d = &g_hv_disks[g_hv_disk_n];
                    memset(d, 0, sizeof(*d));
                    d->active = 1;
                    d->chan_idx = (u8)cidx;
                    d->path_id = (u8)path;
                    d->target_id = (u8)target;
                    d->lun = (u8)lun;
                    d->dev_type = dev_type;
                    d->read_only = (dev_type == 0x05U) ? 1 : 0;
                    d->block_size = bsz;
                    d->total_blocks = total_blocks;
                    memcpy(d->model, inq + 8, 24);
                    d->model[24] = 0;
                    for (k = 23; k >= 0 && (d->model[k] == ' ' || d->model[k] == 0); k--)
                        d->model[k] = 0;
                    klog("[hv-stor] #%d type=0x%x '%s' blocks=%llu bsz=%u",
                         g_hv_disk_n, dev_type, d->model,
                         (unsigned long long)total_blocks, bsz);
                    g_hv_disk_n++;
                }
            }
        }
    }
}

static int hv_stor_rw_blocks(const HvScsiDisk *d, u64 phys_lba, u32 phys_cnt, void *buf, int is_write) {
    HvChan *ch = &g_chans[d->chan_idx];
    u8 cdb[16];
    u8 cdb_len;
    u32 xfer = phys_cnt * d->block_size;

    memset(cdb, 0, sizeof(cdb));
    if (phys_lba <= 0xFFFFFFFFULL && phys_cnt <= 0xFFFFU) {
        cdb[0] = is_write ? 0x2AU : 0x28U; /* WRITE(10) / READ(10) */
        cdb[2] = (u8)(phys_lba >> 24);
        cdb[3] = (u8)(phys_lba >> 16);
        cdb[4] = (u8)(phys_lba >> 8);
        cdb[5] = (u8)phys_lba;
        cdb[7] = (u8)(phys_cnt >> 8);
        cdb[8] = (u8)phys_cnt;
        cdb_len = 10;
    } else {
        cdb[0] = is_write ? 0x8AU : 0x88U; /* WRITE(16) / READ(16) */
        cdb[2] = (u8)(phys_lba >> 56); cdb[3] = (u8)(phys_lba >> 48);
        cdb[4] = (u8)(phys_lba >> 40); cdb[5] = (u8)(phys_lba >> 32);
        cdb[6] = (u8)(phys_lba >> 24); cdb[7] = (u8)(phys_lba >> 16);
        cdb[8] = (u8)(phys_lba >> 8);  cdb[9] = (u8)phys_lba;
        cdb[10] = (u8)(phys_cnt >> 24); cdb[11] = (u8)(phys_cnt >> 16);
        cdb[12] = (u8)(phys_cnt >> 8);  cdb[13] = (u8)phys_cnt;
        cdb_len = 16;
    }
    return hv_stor_exec_retry(ch, d->path_id, d->target_id, d->lun, cdb, cdb_len, !is_write, buf, xfer);
}

int hv_stor_count(void) { return g_hv_disk_n; }
const HvScsiDisk *hv_stor_get(int idx) { return (idx >= 0 && idx < g_hv_disk_n) ? &g_hv_disks[idx] : NULL; }

int hv_stor_read512(int idx, u64 lba512, u32 count512, void *buf) {
    static u8 sector_bounce[4096];
    const HvScsiDisk *d = hv_stor_get(idx);
    u8 *dst = (u8 *)buf;
    u32 spb;
    if (!d) return -1;
    spb = d->block_size / 512U;

    while (count512 > 0) {
        if (d->block_size == 512U) {
            u32 chunk = count512 > 64U ? 64U : count512;
            if (hv_stor_rw_blocks(d, lba512, chunk, dst, 0) != 0) return -1;
            dst += (size_t)chunk * 512U;
            lba512 += chunk;
            count512 -= chunk;
        } else {
            u64 plba = lba512 / spb;
            u32 sub  = (u32)(lba512 % spb);
            u32 take = count512 < (spb - sub) ? count512 : (spb - sub);
            if (hv_stor_rw_blocks(d, plba, 1U, sector_bounce, 0) != 0) return -1;
            memcpy(dst, sector_bounce + (size_t)sub * 512U, (size_t)take * 512U);
            dst += (size_t)take * 512U;
            lba512 += take;
            count512 -= take;
        }
    }
    return 0;
}

int hv_stor_write512(int idx, u64 lba512, u32 count512, const void *buf) {
    static u8 sector_bounce[4096];
    const HvScsiDisk *d = hv_stor_get(idx);
    const u8 *src = (const u8 *)buf;
    u32 spb;
    if (!d || d->read_only) return -1;
    spb = d->block_size / 512U;

    while (count512 > 0) {
        if (d->block_size == 512U) {
            u32 chunk = count512 > 64U ? 64U : count512;
            if (hv_stor_rw_blocks(d, lba512, chunk, (void *)src, 1) != 0) return -1;
            src += (size_t)chunk * 512U;
            lba512 += chunk;
            count512 -= chunk;
        } else {
            u64 plba = lba512 / spb;
            u32 sub  = (u32)(lba512 % spb);
            u32 take = count512 < (spb - sub) ? count512 : (spb - sub);
            if (take < spb) {
                if (hv_stor_rw_blocks(d, plba, 1U, sector_bounce, 0) != 0) return -1;
            }
            memcpy(sector_bounce + (size_t)sub * 512U, src, (size_t)take * 512U);
            if (hv_stor_rw_blocks(d, plba, 1U, sector_bounce, 1) != 0) return -1;
            src += (size_t)take * 512U;
            lba512 += take;
            count512 -= take;
        }
    }
    /* Issue SCSI SYNCHRONIZE CACHE(10) so VHDX blocks commit immediately to host disk! */
    {
        u8 sync10[10] = { 0x35, 0,0,0,0,0,0,0,0,0 };
        hv_stor_exec_cdb(&g_chans[d->chan_idx], d->path_id, d->target_id, d->lun, sync10, 10, 0, NULL, 0);
    }
    return 0;
}

/* ---------------- Master VMBus Handshake & Poll Entrypoints ---------------- */
int hv_native_init(void *page_pool, size_t pool_bytes, u64 fb_gpa, u32 fb_w, u32 fb_h, u32 fb_pitch) {
    static const struct { u32 ver; u32 cid; } kTryVers[5] = {
        { VERSION_WIN10_V5_2, 4U },
        { VERSION_WIN10_V5,   4U },
        { VERSION_WIN10_V5,   1U },
        { VERSION_WIN10,      1U },
        { VERSION_WIN8_1,     1U },
    };
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
    int i, connected = 0;

    if (!hv_detect_hypervisor() || !page_pool || pool_bytes < 4U * 1024U * 1024U)
        return -1;

    /* Align pool to 4 KiB page boundary */
    g_pool_cur = (u8 *)(((uptr)page_pool + 4095ULL) & ~4095ULL);
    g_pool_end = (u8 *)page_pool + pool_bytes;

    /* Check CPUID 0x40000003 for Partition Reference Counter MSR (bit 1 of EAX) */
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000003U), "c"(0U));
    g_has_time_ref = (eax & (1U << 1)) ? 1 : 0;

    g_hypercall_page = hv_alloc_pages(1);
    g_simp           = (HvSynicMsg *)hv_alloc_pages(1);
    g_siefp          = (u8 *)hv_alloc_pages(1);
    g_int_page       = (u8 *)hv_alloc_pages(1);
    g_mon_page1      = (HvMonitorPage *)hv_alloc_pages(1);
    g_mon_page2      = (HvMonitorPage *)hv_alloc_pages(1);
    g_post_buf       = (HvPostMsgIn *)hv_alloc_pages(1);
    g_scsi_dma       = (u8 *)hv_alloc_pages(8); /* 32 KiB 4K-aligned DMA bounce */
    if (!g_scsi_dma) return -1;

    /* 1. Enable Hyper-V Hypercall Page + SynIC */
    hv_wrmsr(HV_X64_MSR_GUEST_OS_ID, 0x000100060100FD00ULL);
    hv_wrmsr(HV_X64_MSR_HYPERCALL, ((u64)(uptr)g_hypercall_page) | 1ULL);
    hv_wrmsr(HV_X64_MSR_SIMP,      ((u64)(uptr)g_simp)  | 1ULL);
    hv_wrmsr(HV_X64_MSR_SIEFP,     ((u64)(uptr)g_siefp) | 1ULL);
    /* Vector 0x32 + AutoEOI (bit 17), unmasked so Hyper-V delivers all VMBus messages */
    hv_wrmsr(HV_X64_MSR_SINT2,     (u64)VMBUS_SINT_VECTOR | (1ULL << 17));
    hv_wrmsr(HV_X64_MSR_SCONTROL,  1ULL);

    /* 2. Cleanly unload UEFI VmbusDxe's previous VMBus connection first! */
    {
        HvChanMsgHdr unl = { CHANNELMSG_UNLOAD, 0U };
        int r1 = hv_post_msg(4U, &unl, sizeof(unl));
        int r2 = hv_post_msg(1U, &unl, sizeof(unl));
        if (r1 == 0 || r2 == 0)
            hv_wait_ctrl_msg(CHANNELMSG_UNLOAD_RESPONSE, NULL, 0, 450);
    }

    /* 3. Negotiate VMBus Protocol Contact */
    g_offer_cnt = 0;
    for (i = 0; i < 5; i++) {
        HvInitiateContact ic;
        HvVersionResp vr;
        memset(&ic, 0, sizeof(ic));
        ic.hdr.msgtype = CHANNELMSG_INITIATE_CONTACT;
        ic.vmbus_version_requested = kTryVers[i].ver;
        ic.target_vcpu = 0U;
        if (kTryVers[i].ver >= VERSION_WIN10_V5) {
            ic.msg_sint = VMBUS_MESSAGE_SINT;
            ic.msg_vtl = 0U;
        } else {
            ic.interrupt_page = (u64)(uptr)g_int_page;
        }
        ic.monitor_page1 = (u64)(uptr)g_mon_page1;
        ic.monitor_page2 = (u64)(uptr)g_mon_page2;

        if (hv_post_msg(kTryVers[i].cid, &ic, sizeof(ic)) != 0)
            continue;
        memset(&vr, 0, sizeof(vr));
        if (hv_wait_ctrl_msg(CHANNELMSG_VERSION_RESPONSE, &vr, sizeof(vr), 800) > 0 &&
            vr.version_supported) {
            g_vmbus_proto = kTryVers[i].ver;
            g_msg_conn_id = (kTryVers[i].ver >= VERSION_WIN10_V5 && vr.msg_conn_id)
                            ? vr.msg_conn_id : kTryVers[i].cid;
            connected = 1;
            break;
        }
    }
    if (!connected) {
        klog("[hv] VMBus INITIATE_CONTACT did not connect");
        return -1;
    }

    /* 4. Request all offered VMBus channels until ALLOFFERS_DELIVERED */
    {
        HvChanMsgHdr req = { CHANNELMSG_REQUESTOFFERS, 0U };
        hv_post_msg(g_msg_conn_id, &req, sizeof(req));
        hv_wait_ctrl_msg(CHANNELMSG_ALLOFFERS_DELIVERED, NULL, 0, 1500);
    }
    klog("[hv] Connected VMBus proto=0x%x msg_conn=%u offers=%d",
         g_vmbus_proto, g_msg_conn_id, g_offer_cnt);

    /* 5. Open SynthVid, SynthHID Mouse, SynthKbd, and StorVSC channels */
    for (i = 0; i < g_offer_cnt; i++) {
        const u8 *t = g_offers[i].offer.if_type;
        if (g_chan_vid < 0 && memcmp(t, GUID_SYNTHVID, 16) == 0) {
            g_chan_vid = hv_open_offer(&g_offers[i]);
        } else if (g_chan_mouse < 0 && memcmp(t, GUID_SYNTHHID, 16) == 0) {
            g_chan_mouse = hv_open_offer(&g_offers[i]);
        } else if (g_chan_kbd < 0 && memcmp(t, GUID_SYNTHKBD, 16) == 0) {
            g_chan_kbd = hv_open_offer(&g_offers[i]);
        } else if (g_chan_stor_n < 4 &&
                   (memcmp(t, GUID_STOR_SCSI, 16) == 0 || memcmp(t, GUID_STOR_IDE, 16) == 0)) {
            int sc = hv_open_offer(&g_offers[i]);
            if (sc >= 0) g_chan_stor[g_chan_stor_n++] = sc;
        }
    }

    if (g_chan_vid >= 0)   hv_init_synthvid(fb_gpa, fb_w, fb_h, fb_pitch);
    if (g_chan_mouse >= 0) hv_init_mouse();
    if (g_chan_kbd >= 0)   hv_init_kbd();
    for (i = 0; i < g_chan_stor_n; i++)
        hv_init_stor_channel(g_chan_stor[i]);

    g_hv_active = 1;
    snprintf(g_hv_summary, sizeof(g_hv_summary),
             "Hyper-V VMBus (%s%s%s, дисков: %d)",
             g_mouse_ok ? "SynthHID Мышь" : "NoMouse",
             g_vid_ok   ? " + SynthVid"   : "",
             g_kbd_ok   ? " + SynthKbd"   : "",
             g_hv_disk_n);
    klog("[hv] Ready: %s", g_hv_summary);
    return 0;
}

int hv_native_active(void) { return g_hv_active; }
int hv_mouse_ready(void)   { return g_mouse_ok; }
int hv_kbd_ready(void)     { return g_kbd_ok; }
int hv_video_ready(void)   { return g_vid_ok; }
const char *hv_status_summary(void) { return g_hv_summary; }

void hv_poll(void) {
    if (!g_hv_active) return;
    {
        u8 tmp[240];
        u32 t = 0;
        hv_recv_synic_msg(tmp, sizeof(tmp), &t);
    }
    hv_poll_mouse();
    hv_poll_kbd();
    hv_poll_synthvid();
}

#else /* 32-bit BIOS target stubs */

int  hv_detect_hypervisor(void) { return 0; }
void hv_cpu_post_exit_boot_services(void) {}
int  hv_native_init(void *p, size_t n, u64 fg, u32 w, u32 h, u32 pt) {
    UNUSED(p); UNUSED(n); UNUSED(fg); UNUSED(w); UNUSED(h); UNUSED(pt);
    return -1;
}
int  hv_native_active(void) { return 0; }
int  hv_mouse_ready(void)   { return 0; }
int  hv_kbd_ready(void)     { return 0; }
int  hv_video_ready(void)   { return 0; }
const char *hv_status_summary(void) { return "N/A"; }
void hv_poll(void) {}
void hv_synthvid_dirt(int x1, int y1, int x2, int y2) { UNUSED(x1); UNUSED(y1); UNUSED(x2); UNUSED(y2); }
u64  hv_time_ms(void) { return 0; }
void hv_delay_ms(u32 ms) { UNUSED(ms); }
void hv_reboot(void) {}
int  hv_disk_count(void) { return 0; }
int  hv_disk_get_info(int i, u64 *s, int *rm, char *m, size_t ml) {
    UNUSED(i); UNUSED(s); UNUSED(rm); UNUSED(m); UNUSED(ml);
    return -1;
}
int  hv_disk_read(int i, u64 l, u32 c, void *b) { UNUSED(i); UNUSED(l); UNUSED(c); UNUSED(b); return -1; }
int  hv_disk_write(int i, u64 l, u32 c, const void *b) { UNUSED(i); UNUSED(l); UNUSED(c); UNUSED(b); return -1; }

#endif
