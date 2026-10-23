/* YoroOS 0.2.2 - Native Bare-Metal Microsoft Hyper-V Generation 2 Driver Stack
 *
 * Implements full x86-64/x86 bare-metal Hyper-V enlightenment after ExitBootServices():
 *   - CPUID Hypervisor Detection + Hypercall Page + SynIC (SIMP / SIEFP / SINT2 / EOM)
 *   - 64-bit GDT + Safe 256-Vector IDT + Masked 8259 PIC / LAPIC LVT Timer
 *   - VMBus UNLOAD (releases UEFI VmbusDxe session) + INITIATE_CONTACT (v5.2..v3.0)
 *   - GPADL + Dual 64 KiB Ring Buffers + Monitor Page + Fast HVCALL_SIGNAL_EVENT
 *   - SynthVid ({da0a7802-...}): VRAM mapping, cursor dot suppression, 60 FPS DIRT blits
 *   - SynthHID ({cfa8b69e-...}): Seamless zero-capture absolute mouse + wheel + buttons
 *   - SynthKbd ({f912ad6d-...}): Full scan-code + Unicode paste synthetic keyboard
 *   - StorVSC  ({ba6163d9-...}): Virtual SCSI/IDE VHDX disk & ISO DVD block I/O
 */
#ifndef YORO_HV_NATIVE_H
#define YORO_HV_NATIVE_H

#include "kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  active;
    u8   chan_idx;
    u8   path_id;
    u8   target_id;
    u8   lun;
    u8   dev_type;       /* 0x00 = Direct-Access VHDX Disk, 0x05 = CD/DVD ISO */
    u8   read_only;
    u32  block_size;     /* 512, 2048, or 4096 */
    u64  total_blocks;
    char model[48];
} HvScsiDisk;

int  hv_detect_hypervisor(void);
void hv_cpu_post_exit_boot_services(void);
int  hv_native_init(void *page_pool, size_t pool_bytes, u64 fb_gpa, u32 fb_w, u32 fb_h, u32 fb_pitch);
int  hv_native_active(void);
int  hv_mouse_ready(void);
int  hv_kbd_ready(void);
int  hv_video_ready(void);
const char *hv_status_summary(void);

u64  hv_time_ref_100ns(void);
void hv_delay_us(u32 us);
void hv_poll(void);
void hv_synthvid_dirt(int x1, int y1, int x2, int y2);
void hv_synthvid_set_mode(u64 fb_gpa, u32 fb_w, u32 fb_h, u32 fb_pitch);
void hv_reboot_hypervisor(void);

int  hv_stor_count(void);
const HvScsiDisk *hv_stor_get(int idx);
int  hv_stor_read512(int idx, u64 lba512, u32 count512, void *buf);
int  hv_stor_write512(int idx, u64 lba512, u32 count512, const void *buf);

#ifdef __cplusplus
}
#endif

#endif
