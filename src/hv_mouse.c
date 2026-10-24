/* Hyper-V synthetic mouse driver: VMBus connection, mouse channel, SynthHID input.
 *
 * Wire formats follow Linux 6.x (include/hyperv/hvgdk_mini.h, hvhdk_mini.h,
 * drivers/hv/{connection,channel,ring_buffer}.c, drivers/hid/hid-hyperv.c).
 * Runs under UEFI boot services: identity-mapped memory, no interrupts.
 */
#include "hv_mouse.h"
#include "hid.h"

#define NULL ((void *)0)

typedef u64 uptr;

/* ------------------------------------------------------------------ libc */

void *memcpy(void *dst, const void *src, u64 n) {
    void *ret = dst;
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memset(void *dst, int c, u64 n) {
    void *ret = dst;
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(c) : "memory");
    return ret;
}

static int bytes_equal(const u8 *a, const u8 *b, u32 n) {
    for (u32 i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* ------------------------------------------------------- MSRs & hypercalls */

#define HV_MSR_GUEST_OS_ID    0x40000000U
#define HV_MSR_HYPERCALL      0x40000001U
#define HV_MSR_TIME_REF_COUNT 0x40000020U
#define HV_MSR_SCONTROL       0x40000080U
#define HV_MSR_SIEFP          0x40000082U
#define HV_MSR_SIMP           0x40000083U
#define HV_MSR_EOM            0x40000084U
#define HV_MSR_SINT2          0x40000092U

#define HVCALL_POST_MESSAGE   0x005CU
#define HVCALL_SIGNAL_EVENT   0x005DU
#define HV_FAST_CALL          0x00010000ULL
#define HV_STATUS_SUCCESS     0x0000ULL
#define HV_STATUS_BAD_CONN    0x0005ULL

#define VMBUS_MSG_SINT        2U
#define VMBUS_SINT_VECTOR     0x32U   /* unused: the SINT stays masked */
#define SINT_MASKED           (1ULL << 16)
#define SINT_AUTO_EOI         (1ULL << 17)

/* Channel message types (CHANNELMSG_*). */
#define CM_OFFERCHANNEL        1U
#define CM_REQUESTOFFERS       3U
#define CM_ALLOFFERS_DELIVERED 4U
#define CM_OPENCHANNEL         5U
#define CM_OPENCHANNEL_RESULT  6U
#define CM_GPADL_HEADER        8U
#define CM_GPADL_BODY          9U
#define CM_GPADL_CREATED       10U
#define CM_INITIATE_CONTACT    14U
#define CM_VERSION_RESPONSE    15U
#define CM_UNLOAD              16U
#define CM_UNLOAD_RESPONSE     17U

#define VERSION_WIN8_1        0x00030000U
#define VERSION_WIN10         0x00040000U
#define VERSION_WIN10_V5      0x00050000U
#define VERSION_WIN10_V5_2    0x00050002U

#define PKT_DATA_INBAND       0x0006U
#define PKT_FLAG_COMPLETION   0x0001U

#define RING_SEND_PAGES       16U
#define RING_RECV_PAGES       16U
#define RING_PAGES            (RING_SEND_PAGES + RING_RECV_PAGES)
#define RING_DATA_BYTES       ((RING_SEND_PAGES - 1U) * 4096U)
#define MAX_OFFERS            64U
#define GPADL_FIRST_PFNS      26U     /* 28 + 26*8 = 236 bytes: fits the 240-byte message payload */
#define GPADL_REST_PFNS       (RING_PAGES - GPADL_FIRST_PFNS)

/* Mouse interface GUID cfa8b69e-5b4a-4cc0-b98b-8ba1a1f3f95a, in wire byte order. */
static const u8 GUID_SYNTHHID[16] = {
    0x9e, 0xb6, 0xa8, 0xcf, 0x4a, 0x5b, 0xc0, 0x4c,
    0xb9, 0x8b, 0x8b, 0xa1, 0xa1, 0xf3, 0xf9, 0x5a
};

static inline u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void wrmsr(u32 msr, u64 val) {
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((u32)val), "d"((u32)(val >> 32)) : "memory");
}

/* ---------------------------------------------------------- wire structs */

typedef struct __attribute__((packed)) {
    u32 msgtype;
    u32 padding;
} ChanMsgHdr;

typedef struct __attribute__((packed)) {
    u32 message_type;
    u8  payload_size;
    u8  message_flags;      /* bit 0: message pending */
    u8  reserved[2];
    u64 sender;
    u8  payload[240];
} SynicMsg;

typedef struct __attribute__((packed)) {
    u32 connection_id;
    u32 reserved;
    u32 message_type;
    u32 payload_size;
    u8  payload[240];
} PostMsgIn;

typedef struct __attribute__((packed)) {
    ChanMsgHdr hdr;
    u32 vmbus_version_requested;
    u32 target_vcpu;
    union {
        u64 interrupt_page;             /* protocol before WIN10_V5 */
        struct __attribute__((packed)) {
            u8  msg_sint;               /* WIN10_V5 and later */
            u8  msg_vtl;
            u8  reserved[2];
            u32 feature_flags;
        };
    };
    u64 monitor_page1;
    u64 monitor_page2;
} InitiateContact;

typedef struct __attribute__((packed)) {
    ChanMsgHdr hdr;
    u8  version_supported;
    u8  connection_state;
    u16 pad2;
    u32 msg_conn_id;
} VersionResp;

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
} ChannelOffer;

typedef struct __attribute__((packed)) {
    ChanMsgHdr   hdr;
    ChannelOffer offer;
    u32 child_relid;
    u8  monitorid;
    u8  monitor_flags;
    u16 dedicated_flags;
    u32 connection_id;
} OfferChannel;

typedef struct __attribute__((packed)) {
    ChanMsgHdr hdr;
    u32 child_relid;
    u32 gpadl;
    u16 range_buflen;
    u16 rangecount;
    u32 byte_count;
    u32 byte_offset;
    u64 pfn[32];
} GpadlHeader;

typedef struct __attribute__((packed)) {
    ChanMsgHdr hdr;
    u32 child_relid;
    u32 gpadl;
    u32 creation_status;
} GpadlCreated;

typedef struct __attribute__((packed)) {
    ChanMsgHdr hdr;
    u32 child_relid;
    u32 openid;
    u32 ringbuffer_gpadlhandle;
    u32 target_vp;
    u32 downstream_ringbuffer_pageoffset;
    u8  userdata[120];
} OpenChannel;

typedef struct __attribute__((packed)) {
    ChanMsgHdr hdr;
    u32 child_relid;
    u32 openid;
    u32 status;
} OpenResult;

typedef struct __attribute__((packed)) {
    volatile u32 write_index;
    volatile u32 read_index;
    volatile u32 interrupt_mask;
    volatile u32 pending_send_sz;
    u32 reserved1[12];
    u32 feature_bits;
    u8  pad_to_page[4096 - 68];
    u8  data[];
} RingBuffer;

typedef struct __attribute__((packed)) {
    u16 type;
    u16 offset8;
    u16 len8;
    u16 flags;
    u64 trans_id;
} PktDesc;

typedef struct __attribute__((packed)) {
    u32 type;               /* 1 = data */
    u32 size;
} PipeHdr;

_Static_assert(sizeof(SynicMsg) == 256, "SynicMsg");
_Static_assert(sizeof(PostMsgIn) == 256, "PostMsgIn");
_Static_assert(sizeof(InitiateContact) == 40, "InitiateContact");
_Static_assert(sizeof(ChannelOffer) == 176, "ChannelOffer");
_Static_assert(sizeof(OfferChannel) == 196, "OfferChannel");
_Static_assert(__builtin_offsetof(GpadlHeader, pfn) == 28, "GpadlHeader pfn offset");
_Static_assert(sizeof(OpenChannel) == 148, "OpenChannel");
_Static_assert(sizeof(RingBuffer) == 4096, "RingBuffer");
_Static_assert(sizeof(PktDesc) == 16, "PktDesc");

/* ----------------------------------------------------------- driver state */

typedef struct {
    u32 relid;
    u32 conn_id;
    u8  monitor_id;
    u8  monitor_allocated;
    u8  dedicated_irq;
    u32 gpadl_handle;
    RingBuffer *send_ring;
    RingBuffer *recv_ring;
    u64 next_req_id;
    int open;
} Chan;

static struct {
    int ready;
    int has_time_ref;
    /* pool layout, all page-aligned */
    u8 *hypercall_page;
    SynicMsg *simp;
    u8 *siefp;
    u8 *int_page;
    u8 *mon1;
    u8 *mon2;
    PostMsgIn *post_buf;
    u8 *ring_base;

    u32 vmbus_proto;
    u32 msg_conn_id;
    u32 next_gpadl;
    OfferChannel offers[MAX_OFFERS];
    u32 offer_cnt;

    Chan mouse;
    HidMouseLayout layout;
    int mouse_ok;

    u32 x, y, buttons;
    u32 max_x, max_y;
} g;

static u8 g_rx[8192];

/* ------------------------------------------------------------- timing */

static u64 time_100ns(void) {
    if (g.has_time_ref) return rdmsr(HV_MSR_TIME_REF_COUNT);
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (((u64)hi << 32) | lo) / 250ULL;   /* rough: only used for timeouts */
}

static void delay_us(u32 us) {
    volatile u32 spin = us * 400U;
    while (spin--) __asm__ volatile("pause");
}

/* ----------------------------------------------------------- hypercalls */

static u64 hypercall(u64 control, u64 input, u64 output) {
    u64 status;
    register u64 r8 __asm__("r8") = output;
    __asm__ volatile("call *%[hc]"
                     : "=a"(status), "+c"(control), "+d"(input), "+r"(r8)
                     : [hc] "r"(g.hypercall_page)
                     : "r9", "r10", "r11", "cc", "memory");
    return status;
}

int hvm_detect(void) {
    u32 eax, ebx, ecx, edx;
    char sig[13];
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1U), "c"(0U));
    if (!(ecx & (1U << 31))) return 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000000U), "c"(0U));
    memcpy(sig + 0, &ebx, 4);
    memcpy(sig + 4, &ecx, 4);
    memcpy(sig + 8, &edx, 4);
    sig[12] = 0;
    return bytes_equal((const u8 *)sig, (const u8 *)"Microsoft Hv", 12);
}

/* ------------------------------------------------------- SynIC messages */

static int post_msg(u32 conn_id, const void *msg, u32 len) {
    if (len > 240U) return -1;
    for (int tries = 0; tries < 200; tries++) {
        u64 st;
        memset(g.post_buf, 0, sizeof(*g.post_buf));
        g.post_buf->connection_id = conn_id;
        g.post_buf->message_type = 1U;
        g.post_buf->payload_size = len;
        memcpy(g.post_buf->payload, msg, len);
        st = hypercall(HVCALL_POST_MESSAGE, (u64)(uptr)g.post_buf, 0) & 0xFFFFULL;
        if (st == HV_STATUS_SUCCESS) return 0;
        if (st == HV_STATUS_BAD_CONN) return -5;
        delay_us(250);
    }
    return -1;
}

/* Takes one message from the SIMP slot, acknowledges it, returns its payload size or 0. */
static int recv_synic(u8 *out, u32 max_len, u32 *out_type) {
    volatile SynicMsg *slot = &g.simp[VMBUS_MSG_SINT];
    u32 mtype = slot->message_type;
    u8 psize;
    if (mtype == 0) return 0;
    __asm__ volatile("" ::: "memory");
    psize = slot->payload_size;
    if (psize > max_len) psize = (u8)max_len;
    memcpy(out, (const void *)slot->payload, psize);
    if (out_type) *out_type = *(const u32 *)out;
    slot->message_type = 0;
    __asm__ volatile("mfence" ::: "memory");
    wrmsr(HV_MSR_EOM, 0);
    return psize;
}

static void record_offer(const u8 *buf, u32 len) {
    if (len >= sizeof(OfferChannel) && g.offer_cnt < MAX_OFFERS) {
        memcpy(&g.offers[g.offer_cnt], buf, sizeof(OfferChannel));
        g.offer_cnt++;
    }
}

/* Waits for a control message of want_type; other offers seen on the way are recorded. */
static int wait_ctrl(u32 want_type, void *out, u32 max_len, u32 timeout_ms) {
    u64 start = time_100ns();
    u64 budget = (u64)timeout_ms * 10000ULL;
    u8 tmp[240];
    while (time_100ns() - start < budget) {
        u32 got = 0;
        int n = recv_synic(tmp, sizeof(tmp), &got);
        if (n <= 0) continue;
        if (got == want_type) {
            u32 cp = (u32)n < max_len ? (u32)n : max_len;
            if (out) memcpy(out, tmp, cp);
            return (int)cp;
        }
        if (got == CM_OFFERCHANNEL) record_offer(tmp, (u32)n);
    }
    return -1;
}

/* ---------------------------------------------------------- ring buffers */

static void signal_channel(Chan *ch) {
    /* Event bitmap bit for legacy channels. */
    if (!ch->dedicated_irq && g.int_page && ch->relid < 2048U) {
        u32 *send_bmp = (u32 *)(g.int_page + 2048U);
        __sync_fetch_and_or(&send_bmp[ch->relid >> 5], 1U << (ch->relid & 31U));
    }
    /* Monitor trigger bit. */
    if (ch->monitor_allocated && g.mon2) {
        u32 grp = ch->monitor_id >> 5;
        u32 bit = 1U << (ch->monitor_id & 31U);
        if (grp < 4U) {
            u32 *pend = (u32 *)(g.mon2 + 8U + grp * 8U);
            __asm__ volatile("lock orl %1, %0" : "+m"(*pend) : "r"(bit) : "memory");
        }
    }
    /* Fast hypercall: wakes the host side of the channel. */
    hypercall(HVCALL_SIGNAL_EVENT | HV_FAST_CALL, (u64)ch->conn_id, 0);
}

static u32 ring_free_write(const RingBuffer *r) {
    u32 w = r->write_index, rd = r->read_index;
    return w >= rd ? RING_DATA_BYTES - (w - rd) : (rd - w);
}

static u32 ring_avail_read(const RingBuffer *r) {
    u32 w = r->write_index, rd = r->read_index;
    __asm__ volatile("" ::: "memory");
    return w >= rd ? (w - rd) : (RING_DATA_BYTES - rd + w);
}

static u32 ring_put(RingBuffer *r, u32 pos, const void *src, u32 len) {
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

static void ring_get(const RingBuffer *r, u32 pos, void *dst, u32 len) {
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

static int chan_write(Chan *ch, const void *desc, u32 desc_bytes, const void *payload, u32 pay_bytes) {
    static const u8 zpad[8] = {0};
    RingBuffer *r = ch->send_ring;
    u32 unaligned = desc_bytes + pay_bytes;
    u32 pad = (8U - (unaligned & 7U)) & 7U;
    u32 total = unaligned + pad + 8U;     /* +8: the previous-indices trailer */
    u32 old_w, pos;
    u64 prev_indices;

    if (ring_free_write(r) <= total) return -1;
    old_w = r->write_index;
    pos = ring_put(r, old_w, desc, desc_bytes);
    if (pay_bytes) pos = ring_put(r, pos, payload, pay_bytes);
    if (pad) pos = ring_put(r, pos, zpad, pad);
    prev_indices = (u64)old_w << 32;
    pos = ring_put(r, pos, &prev_indices, 8U);
    __asm__ volatile("mfence" ::: "memory");
    r->write_index = pos;
    __asm__ volatile("mfence" ::: "memory");
    if (!r->interrupt_mask) signal_channel(ch);
    return 0;
}

static int chan_send_pipe(Chan *ch, const u8 *msg, u32 msg_len, u16 flags) {
    PipeHdr ph;
    PktDesc d;
    u8 pay[256];
    u32 aligned;

    if (msg_len + sizeof(ph) > sizeof(pay)) return -1;
    ph.type = 1U;
    ph.size = msg_len;
    memcpy(pay, &ph, sizeof(ph));
    memcpy(pay + sizeof(ph), msg, msg_len);
    aligned = (16U + (u32)sizeof(ph) + msg_len + 7U) & ~7U;
    d.type = PKT_DATA_INBAND;
    d.offset8 = 2;
    d.len8 = (u16)(aligned >> 3);
    d.flags = flags;
    d.trans_id = ch->next_req_id++;
    return chan_write(ch, &d, sizeof(d), pay, sizeof(ph) + msg_len);
}

/* Returns the payload length of one received packet, or 0 if none is ready. */
static int chan_recv(Chan *ch, void *out, u32 max_len) {
    RingBuffer *r = ch->recv_ring;
    PktDesc d;
    u32 avail, rd, pkt_bytes, hdr_bytes, pay_bytes, next_rd;

    avail = ring_avail_read(r);
    if (avail < sizeof(PktDesc) + 8U) return 0;
    rd = r->read_index;
    ring_get(r, rd, &d, sizeof(d));
    pkt_bytes = (u32)d.len8 << 3;
    hdr_bytes = (u32)d.offset8 << 3;
    if (pkt_bytes < hdr_bytes || avail < pkt_bytes + 8U) return 0;

    pay_bytes = pkt_bytes - hdr_bytes;
    if (pay_bytes > 0 && out && max_len > 0) {
        u32 cp = pay_bytes < max_len ? pay_bytes : max_len;
        ring_get(r, (rd + hdr_bytes) % RING_DATA_BYTES, out, cp);
    }
    next_rd = (rd + pkt_bytes + 8U) % RING_DATA_BYTES;
    __asm__ volatile("mfence" ::: "memory");
    r->read_index = next_rd;
    __asm__ volatile("mfence" ::: "memory");
    if (r->pending_send_sz != 0 && !r->interrupt_mask) signal_channel(ch);
    return (int)pay_bytes;
}

/* ------------------------------------------------------ channel opening */

static int open_offer(const OfferChannel *of, Chan *ch) {
    GpadlHeader gp;
    GpadlCreated gpc;
    OpenChannel oc;
    OpenResult ocr;
    u8 hdr_buf[240];
    u8 body_buf[16 + GPADL_REST_PFNS * 8U];
    ChanMsgHdr *bh = (ChanMsgHdr *)body_buf;
    u64 base_pfn = (u64)(uptr)g.ring_base >> 12;

    memset(ch, 0, sizeof(*ch));
    ch->relid = of->child_relid;
    ch->conn_id = of->connection_id;
    ch->monitor_id = of->monitorid;
    ch->monitor_allocated = (of->monitor_flags & 1U) ? 1 : 0;
    ch->dedicated_irq = (of->dedicated_flags & 1U) ? 1 : 0;
    ch->gpadl_handle = g.next_gpadl++;
    ch->send_ring = (RingBuffer *)g.ring_base;
    ch->recv_ring = (RingBuffer *)(g.ring_base + RING_SEND_PAGES * 4096U);
    ch->send_ring->feature_bits = 1U;
    ch->recv_ring->feature_bits = 1U;
    ch->next_req_id = 1;

    /* The ring pages are one contiguous allocation, so the PFN list is consecutive. */
    memset(&gp, 0, sizeof(gp));
    gp.hdr.msgtype = CM_GPADL_HEADER;
    gp.child_relid = ch->relid;
    gp.gpadl = ch->gpadl_handle;
    gp.range_buflen = (u16)(8U + RING_PAGES * 8U);
    gp.rangecount = 1;
    gp.byte_count = RING_PAGES * 4096U;
    gp.byte_offset = 0;
    for (u32 i = 0; i < RING_PAGES; i++) gp.pfn[i] = base_pfn + i;

    memcpy(hdr_buf, &gp, 28U + GPADL_FIRST_PFNS * 8U);
    if (post_msg(g.msg_conn_id, hdr_buf, 28U + GPADL_FIRST_PFNS * 8U) != 0) return -1;

    memset(body_buf, 0, sizeof(body_buf));
    bh->msgtype = CM_GPADL_BODY;
    *(u32 *)(body_buf + 8) = 0U;                 /* message number */
    *(u32 *)(body_buf + 12) = ch->gpadl_handle;
    memcpy(body_buf + 16, &gp.pfn[GPADL_FIRST_PFNS], GPADL_REST_PFNS * 8U);
    if (post_msg(g.msg_conn_id, body_buf, 16U + GPADL_REST_PFNS * 8U) != 0) return -1;

    memset(&gpc, 0, sizeof(gpc));
    if (wait_ctrl(CM_GPADL_CREATED, &gpc, sizeof(gpc), 1500) < 0 || gpc.creation_status != 0)
        return -1;

    memset(&oc, 0, sizeof(oc));
    oc.hdr.msgtype = CM_OPENCHANNEL;
    oc.child_relid = ch->relid;
    oc.openid = ch->relid;
    oc.ringbuffer_gpadlhandle = ch->gpadl_handle;
    oc.target_vp = 0;
    oc.downstream_ringbuffer_pageoffset = RING_SEND_PAGES;
    if (post_msg(g.msg_conn_id, &oc, sizeof(oc)) != 0) return -1;

    memset(&ocr, 0, sizeof(ocr));
    if (wait_ctrl(CM_OPENCHANNEL_RESULT, &ocr, sizeof(ocr), 1500) < 0 || ocr.status != 0)
        return -1;

    ch->open = 1;
    return 0;
}

/* ------------------------------------------------------------- SynthHID */

static u32 xtract(const u8 *rep, int rlen, int bit, int cnt) {
    return hid_extract_bits(rep, rlen, bit, cnt);
}

static int init_mouse(void) {
    u8 req[12];
    u64 start = time_100ns();
    int got_info = 0;

    /* SYNTH_HID_PROTOCOL_REQUEST: type 0, size 4, version 2.0 */
    *(u32 *)(req + 0) = 0U;
    *(u32 *)(req + 4) = 4U;
    *(u32 *)(req + 8) = 0x00020000U;
    chan_send_pipe(&g.mouse, req, 12U, PKT_FLAG_COMPLETION);

    while (time_100ns() - start < 10000000ULL && !got_info) {
        int n = chan_recv(&g.mouse, g_rx, sizeof(g_rx));
        if (n < 16) continue;
        {
            u32 mtype = *(u32 *)(g_rx + 8);
            if (mtype == 2U) {   /* SYNTH_HID_INITIAL_DEVICE_INFO */
                /* pipe 8 + synthhid hdr 8 + device info 32 + hid descriptor */
                u16 rdesc_len = *(u16 *)(g_rx + 16 + 32 + 7);
                int rdesc_off = 16 + 32 + 9;
                if (rdesc_len > 0 && rdesc_off + (int)rdesc_len <= n)
                    hid_parse_mouse(g_rx + rdesc_off, rdesc_len, &g.layout);
                if (g.layout.x_size == 0 || g.layout.y_size == 0) {
                    /* Descriptor missing or unreadable: the layout Hyper-V uses for its absolute mouse. */
                    g.layout.has_report_id = 0;
                    g.layout.btn_bit = 0;  g.layout.btn_size = 8;
                    g.layout.x_bit = 8;    g.layout.x_size = 16; g.layout.x_max = 32767;
                    g.layout.y_bit = 24;   g.layout.y_size = 16; g.layout.y_max = 32767;
                    g.layout.wheel_bit = 40; g.layout.wheel_size = 8;
                }
                {
                    u8 ack[9];
                    *(u32 *)(ack + 0) = 3U;      /* SYNTH_HID_INITIAL_DEVICE_INFO_ACK */
                    *(u32 *)(ack + 4) = 1U;
                    ack[8] = 0;
                    chan_send_pipe(&g.mouse, ack, 9U, PKT_FLAG_COMPLETION);
                }
                got_info = 1;
            }
        }
    }
    if (!got_info) return -1;
    g.max_x = g.layout.x_max;
    g.max_y = g.layout.y_max;
    return 0;
}

/* ------------------------------------------------------------------ init */

static const struct { u32 ver; u32 cid; } TRY_VERSIONS[5] = {
    { VERSION_WIN10_V5_2, 4U },
    { VERSION_WIN10_V5,   4U },
    { VERSION_WIN10_V5,   1U },
    { VERSION_WIN10,      1U },
    { VERSION_WIN8_1,     1U },
};

static void carve_pool(u8 *pool) {
    u8 *p = pool;
    g.hypercall_page = p; p += 4096;
    g.simp = (SynicMsg *)p; p += 4096;
    g.siefp = p; p += 4096;
    g.int_page = p; p += 4096;
    g.mon1 = p; p += 4096;
    g.mon2 = p; p += 4096;
    g.post_buf = (PostMsgIn *)p; p += 4096;
    g.ring_base = p; p += RING_PAGES * 4096U;
}

int hvm_init(u8 *pool, u64 pool_bytes) {
    int connected = 0;

    if (!hvm_detect()) return -1;
    if (!pool || pool_bytes < (u64)HVM_POOL_PAGES * 4096ULL) return -2;

    /* The pool is page-aligned (UEFI page allocation) and identity-mapped. */
    memset(pool, 0, (u64)HVM_POOL_PAGES * 4096ULL);
    carve_pool(pool);

    {
        u32 eax, ebx, ecx, edx;
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x40000003U), "c"(0U));
        g.has_time_ref = (eax & (1U << 1)) ? 1 : 0;
    }

    /* Enable the hypercall page and SynIC. The message SINT stays masked, so no
     * interrupt is delivered; messages are read from the SIMP slot instead. */
    wrmsr(HV_MSR_GUEST_OS_ID, 0x000100060100FD00ULL);
    wrmsr(HV_MSR_HYPERCALL, (u64)(uptr)g.hypercall_page | 1ULL);
    wrmsr(HV_MSR_SIMP, (u64)(uptr)g.simp | 1ULL);
    wrmsr(HV_MSR_SIEFP, (u64)(uptr)g.siefp | 1ULL);
    wrmsr(HV_MSR_SINT2, (u64)VMBUS_SINT_VECTOR | SINT_MASKED | SINT_AUTO_EOI);
    wrmsr(HV_MSR_SCONTROL, 1ULL);

    /* Drop any connection the firmware's VMBus driver still holds. */
    {
        ChanMsgHdr unl = { CM_UNLOAD, 0 };
        int r1 = post_msg(4U, &unl, sizeof(unl));
        int r2 = post_msg(1U, &unl, sizeof(unl));
        if (r1 == 0 || r2 == 0) wait_ctrl(CM_UNLOAD_RESPONSE, NULL, 0, 450);
    }

    g.next_gpadl = 0x000E1000U;
    g.offer_cnt = 0;
    for (u32 i = 0; i < 5 && !connected; i++) {
        InitiateContact ic;
        VersionResp vr;
        memset(&ic, 0, sizeof(ic));
        ic.hdr.msgtype = CM_INITIATE_CONTACT;
        ic.vmbus_version_requested = TRY_VERSIONS[i].ver;
        ic.target_vcpu = 0;
        if (TRY_VERSIONS[i].ver >= VERSION_WIN10_V5) {
            ic.msg_sint = VMBUS_MSG_SINT;
            ic.msg_vtl = 0;
        } else {
            ic.interrupt_page = (u64)(uptr)g.int_page;
        }
        ic.monitor_page1 = (u64)(uptr)g.mon1;
        ic.monitor_page2 = (u64)(uptr)g.mon2;
        if (post_msg(TRY_VERSIONS[i].cid, &ic, sizeof(ic)) != 0) continue;
        memset(&vr, 0, sizeof(vr));
        if (wait_ctrl(CM_VERSION_RESPONSE, &vr, sizeof(vr), 800) > 0 && vr.version_supported) {
            g.vmbus_proto = TRY_VERSIONS[i].ver;
            g.msg_conn_id = (TRY_VERSIONS[i].ver >= VERSION_WIN10_V5 && vr.msg_conn_id)
                                ? vr.msg_conn_id : TRY_VERSIONS[i].cid;
            connected = 1;
        }
    }
    if (!connected) return -3;

    /* Ask for every offered channel, then wait for the end marker. */
    {
        ChanMsgHdr req = { CM_REQUESTOFFERS, 0 };
        post_msg(g.msg_conn_id, &req, sizeof(req));
        wait_ctrl(CM_ALLOFFERS_DELIVERED, NULL, 0, 1500);
    }

    /* Open the mouse channel only. */
    for (u32 i = 0; i < g.offer_cnt; i++) {
        if (bytes_equal(g.offers[i].offer.if_type, GUID_SYNTHHID, 16)) {
            if (open_offer(&g.offers[i], &g.mouse) == 0) {
                if (init_mouse() == 0) g.mouse_ok = 1;
            }
            break;
        }
    }
    if (!g.mouse_ok) return -4;

    g.x = g.max_x / 2;
    g.y = g.max_y / 2;
    g.ready = 1;
    return 0;
}

/* ------------------------------------------------------------------ poll */

int hvm_poll(void) {
    int changed = 0;
    if (!g.ready) return 0;
    for (int guard = 0; guard < 64; guard++) {
        int n = chan_recv(&g.mouse, g_rx, sizeof(g_rx));
        if (n <= 0) break;
        if (n >= 16 + 5 && *(u32 *)(g_rx + 8) == 4U) {   /* SYNTH_HID_INPUT_REPORT */
            const u8 *rep = g_rx + 16;
            int rlen = n - 16;
            u32 btns = xtract(rep, rlen, g.layout.btn_bit, g.layout.btn_size) & 7U;
            u32 rx = xtract(rep, rlen, g.layout.x_bit, g.layout.x_size);
            u32 ry = xtract(rep, rlen, g.layout.y_bit, g.layout.y_size);
            if (rx > g.max_x) rx = g.max_x;
            if (ry > g.max_y) ry = g.max_y;
            if (rx != g.x || ry != g.y || btns != g.buttons) changed = 1;
            g.x = rx;
            g.y = ry;
            g.buttons = btns;
        }
    }
    return changed;
}

void hvm_pointer(u32 *x, u32 *y, u32 *max_x, u32 *max_y, u32 *buttons) {
    if (x) *x = g.x;
    if (y) *y = g.y;
    if (max_x) *max_x = g.max_x;
    if (max_y) *max_y = g.max_y;
    if (buttons) *buttons = g.buttons;
}
