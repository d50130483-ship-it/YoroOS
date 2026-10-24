/* Hyper-V synthetic mouse (SynthHID over VMBus), polled from the main loop.
 *
 * The driver talks to the host directly: it connects to VMBus, opens the mouse
 * channel and reads input reports. Interrupts stay masked; the host's message
 * page and the channel ring buffers are polled instead. Only Hyper-V guests
 * (hv_mouse_detect() == 1) get a mouse; other machines keep the static pointer.
 */
#ifndef YORO_HV_MOUSE_H
#define YORO_HV_MOUSE_H

#include "efi.h"

/* Pages the driver needs from the caller (contiguous, identity-mapped, executable). */
#define HVM_POOL_PAGES 64U

/* 1 if running under the Microsoft hypervisor. Touches only CPUID. */
int hvm_detect(void);

/* Called at each step of hvm_init() with the step number (debug aid). */
typedef void (*hvm_trace_fn)(u32 step);

/* Connects to VMBus and opens the mouse channel. Returns 0 on success, or a negative error code. */
int hvm_init(u8 *pool, u64 pool_bytes, hvm_trace_fn trace);

/* Drains pending input. Returns 1 if the pointer position or buttons changed. */
int hvm_poll(void);

/* Current raw pointer: x, y in [0, max_x] and [0, max_y]; buttons bit0 left, bit1 right, bit2 middle. */
void hvm_pointer(u32 *x, u32 *y, u32 *max_x, u32 *max_y, u32 *buttons);

#endif
