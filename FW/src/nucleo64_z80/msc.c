/*
 * Copyright (c) 2026 @hanyazou
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "msc.h"

#include "app_usbx_host.h"

#include "ux_api.h"
#include "ux_host_stack.h"
#include "tx_api.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Optional API: some USBX builds don't provide ux_host_stack_endpoint_reset().
 * Provide a weak no-op stub so the app can link.
 */
UINT ux_host_stack_endpoint_reset(UX_ENDPOINT* endpoint);
#if defined(__GNUC__)
__attribute__((weak)) UINT ux_host_stack_endpoint_reset(UX_ENDPOINT* endpoint)
{
    (void)endpoint;
#ifdef UX_FUNCTION_NOT_SUPPORTED
    return UX_FUNCTION_NOT_SUPPORTED;
#else
    return UX_ERROR;
#endif
}
#endif

/*
 * USB FDD (TEAC) = Mass Storage / UFI / CBI (Control/Bulk/Interrupt)
 *
 * Current observation (from your debugger/logs):
 * - Bulk transfers return (with non-success completion_code), BUT the code can hang
 *   when starting the Interrupt-IN status phase.
 * - You often break inside _ux_hcd_stm32_request_periodic_transfer(), which is the
 *   HCD path used for periodic (interrupt) transfers.
 *
 * So, for progress/debug:
 * - We make the Interrupt status phase OPTIONAL and disabled by default.
 * - We focus on verifying ADSC + Bulk-IN data delivery first.
 *
 * If/when Bulk IN succeeds consistently, we can re-enable the interrupt status phase.
 */

/* ------------------- Tuning ------------------- */

#ifndef MSC_TIMEOUT_MS
#define MSC_TIMEOUT_MS   (30000u)
#endif

#ifndef MSC_READ_BLOCK_SIZE
#define MSC_READ_BLOCK_SIZE 512u
#endif

#ifndef MSC_BULK_TRY_TIMEOUT_MS
#define MSC_BULK_TRY_TIMEOUT_MS  500u
#endif

#ifndef MSC_POLL_SLEEP_MS
#define MSC_POLL_SLEEP_MS  50u
#endif

/* Auto-recover from STALL by resetting the endpoint (best-effort). */
#ifndef MSC_AUTO_CLEAR_STALL
#define MSC_AUTO_CLEAR_STALL 1
#endif

/* 0: skip INT-IN status phase (disabled for now) */
#ifndef MSC_USE_INT_STATUS
#define MSC_USE_INT_STATUS 0
#endif

static ULONG ms_to_ticks(ULONG ms)
{
    const ULONG tps = (ULONG)TX_TIMER_TICKS_PER_SECOND;
    return (ms * tps + 999u) / 1000u;
}

/* ------------------- Wait/Notify ------------------- */

static TX_SEMAPHORE g_msc_sem;
static volatile UINT g_sem_inited = 0;
static UX_DEVICE * volatile g_ux_dev = UX_NULL;

void msc_init(void)
{
    if (g_sem_inited) return;
    if (tx_semaphore_create(&g_msc_sem, (CHAR *)"msc_sem", 0) == TX_SUCCESS) {
        g_sem_inited = 1;
    }
}

void msc_notify(void *ux_dev)
{
    g_ux_dev = (UX_DEVICE *)ux_dev;
    if (g_sem_inited) (void)tx_semaphore_put(&g_msc_sem);
}

void msc_wait(void)
{
    if (!g_sem_inited) {
        while (g_ux_dev == UX_NULL) tx_thread_sleep(1);
        return;
    }
    (void)tx_semaphore_get(&g_msc_sem, TX_WAIT_FOREVER);
}

/* ------------------- Utilities ------------------- */

static void dump_hex(const char* title, const UCHAR* data, ULONG len)
{
    const ULONG kCols = 16;

    if (title != NULL) {
        printf("%s (len=%lu)\r\n", title, (unsigned long)len);
    }

    for (ULONG off = 0; off < len; off += kCols) {
        // offset
        printf("%08lu: ", (unsigned long)off);

        // hex bytes (16 columns)
        for (ULONG i = 0; i < kCols; ++i) {
            ULONG idx = off + i;
            if (idx < len) {
                printf("%02X ", (unsigned)data[idx]);
            } else {
                printf("   ");
            }
            if (i == 7) printf(" ");  // small gap in the middle
        }

        // ascii
        printf(" |");
        for (ULONG i = 0; i < kCols; ++i) {
            ULONG idx = off + i;
            if (idx < len) {
                unsigned char c = (unsigned char)data[idx];
                printf("%c", isprint(c) ? (char)c : '.');
            } else {
                printf(" ");
            }
        }
        printf("|\r\n");
    }
}

static void dump_ep(const char *name, UX_ENDPOINT *ep)
{
    const UX_ENDPOINT_DESCRIPTOR *d = &ep->ux_endpoint_descriptor;
    printf("[msc] %s: addr=0x%02X attr=0x%02X maxpkt=%u interval=%u\r\n",
           name,
           (unsigned)d->bEndpointAddress,
           (unsigned)d->bmAttributes,
           (unsigned)d->wMaxPacketSize,
           (unsigned)d->bInterval);
}

static const char *ux_status_str(UINT s)
{
    switch (s) {
    case UX_SUCCESS: return "UX_SUCCESS";
    case UX_ERROR: return "UX_ERROR";
#ifdef UX_STATE_EXIT
    case UX_STATE_EXIT: return "UX_STATE_EXIT";
#endif
#ifdef UX_STATE_WAIT
    case UX_STATE_WAIT: return "UX_STATE_WAIT";
#endif
#ifdef UX_STATE_IDLE
    case UX_STATE_IDLE: return "UX_STATE_IDLE";
#endif
#ifdef UX_STATE_BUSY
    case UX_STATE_BUSY: return "UX_STATE_BUSY";
#endif
#ifdef UX_TRANSFER_STALLED
    case UX_TRANSFER_STALLED: return "UX_TRANSFER_STALLED";
#endif
#ifdef UX_TRANSFER_TIMEOUT
    case UX_TRANSFER_TIMEOUT: return "UX_TRANSFER_TIMEOUT";
#endif
    default: return "UX_*(other)";
    }
}

static void print_xfer(const char *tag, UX_TRANSFER *t, UINT call_status)
{
    printf("[msc] %s: call=%s(%u) cc=%s(%u) actual=%lu req=%lu timeout_ticks=%lu\r\n",
           tag,
           ux_status_str(call_status), (unsigned)call_status,
           ux_status_str(t->ux_transfer_request_completion_code),
           (unsigned)t->ux_transfer_request_completion_code,
           (unsigned long)t->ux_transfer_request_actual_length,
           (unsigned long)t->ux_transfer_request_requested_length,
           (unsigned long)t->ux_transfer_request_timeout_value);
}

/* ------------------- Stack activation / endpoint lookup ------------------- */

static UINT activate_config_and_interface(UX_DEVICE *dev,
                                         UX_CONFIGURATION **out_cfg,
                                         UX_INTERFACE **out_itf)
{
    *out_cfg = UX_NULL;
    *out_itf = UX_NULL;

    UX_CONFIGURATION *cfg = UX_NULL;
    UINT st = ux_host_stack_device_configuration_get(dev, 0, &cfg);
    if (st != UX_SUCCESS || cfg == UX_NULL) return st;

    st = ux_host_stack_device_configuration_select(cfg);
    if (st != UX_SUCCESS) return st;

    (void)ux_host_stack_device_configuration_activate(cfg);

    UX_INTERFACE *itf = UX_NULL;
    st = ux_host_stack_configuration_interface_get(cfg, 0, 0, &itf);
    if (st != UX_SUCCESS || itf == UX_NULL) return st;

    st = ux_host_stack_interface_setting_select(itf);
    if (st != UX_SUCCESS) return st;

    *out_cfg = cfg;
    *out_itf = itf;
    return UX_SUCCESS;
}

static UINT find_ep_by_addr(UX_INTERFACE *itf, UCHAR addr, UX_ENDPOINT **out_ep)
{
    *out_ep = UX_NULL;
    for (UINT i = 0; i < 8; i++) {
        UX_ENDPOINT *ep = UX_NULL;
        UINT st = ux_host_stack_interface_endpoint_get(itf, i, &ep);
        if (st != UX_SUCCESS || ep == UX_NULL) break;
        if (ep->ux_endpoint_descriptor.bEndpointAddress == addr) {
            *out_ep = ep;
            return UX_SUCCESS;
        }
    }
    return UX_ERROR;
}

static UINT get_cbi_eps(UX_INTERFACE *itf, UX_ENDPOINT **ep_out, UX_ENDPOINT **ep_in, UX_ENDPOINT **ep_int)
{
    *ep_out = *ep_in = *ep_int = UX_NULL;
    (void)find_ep_by_addr(itf, 0x01, ep_out);
    (void)find_ep_by_addr(itf, 0x82, ep_in);
    (void)find_ep_by_addr(itf, 0x83, ep_int);
#if MSC_USE_INT_STATUS
    return (*ep_out && *ep_in && *ep_int) ? UX_SUCCESS : UX_ERROR;
#else
    (void)ep_int;
    return (*ep_out && *ep_in) ? UX_SUCCESS : UX_ERROR;
#endif
}

/* ------------------- Transfers ------------------- */

static UINT is_state_machine_code(UINT cc)
{
    /*
     * USBX error-code space:
     *   0x0x : State machine return codes (UX_STATE_*)
     * These may appear transiently in ux_transfer_request_completion_code
     * while the transfer is still in progress.
     */
    if (cc == UX_SUCCESS) return 0;
    if ((cc & 0xF0u) == 0x00u) return 1;
    return 0;
}

static UINT wait_transfer_complete(UX_TRANSFER *t, ULONG timeout_ticks, const char *tag)
{
    /*
     * IMPORTANT:
     * Do NOT re-issue a transfer request when completion_code is a UX_STATE_* value.
     * That just spams the bus/HCD and tends to produce endless UX_STATE_EXIT loops.
     *
     * Instead, issue the transfer once and poll completion_code until it becomes
     * UX_SUCCESS or a real transport error (UX_TRANSFER_*), or until we time out.
     */
    ULONG waited = 0;
    UINT last_cc = 0xFFFFFFFFu;
    const ULONG sleep_ticks = ms_to_ticks(MSC_POLL_SLEEP_MS);

    while (waited < timeout_ticks) {
        UINT cc = t->ux_transfer_request_completion_code;

        if (!is_state_machine_code(cc)) {
            return cc;
        }

        /* Optional: print only when state changes (reduces serial spam). */
        if (cc != last_cc) {
            printf("[msc] %s: cc=%s(%u) ...waiting\r\n", tag,
                   ux_status_str(cc), (unsigned)cc);
            last_cc = cc;
        }

        tx_thread_sleep(sleep_ticks);
        waited += sleep_ticks;
    }

#ifdef UX_TRANSFER_TIMEOUT
    return UX_TRANSFER_TIMEOUT;
#else
    return UX_ERROR;
#endif
}

static UINT endpoint_xfer_once(UX_ENDPOINT *ep, UCHAR *buf, ULONG len, ULONG timeout_ms, const char *tag)
{
    UX_TRANSFER *t = &ep->ux_endpoint_transfer_request;

    t->ux_transfer_request_type = 0;
    t->ux_transfer_request_function = 0;
    t->ux_transfer_request_value = 0;
    t->ux_transfer_request_index = 0;

    t->ux_transfer_request_endpoint = ep;
    t->ux_transfer_request_data_pointer = buf;
    t->ux_transfer_request_requested_length = len;
    t->ux_transfer_request_timeout_value = ms_to_ticks(timeout_ms);

    t->ux_transfer_request_maximum_length = len;
    t->ux_transfer_request_packet_length  = ep->ux_endpoint_descriptor.wMaxPacketSize;

    UINT st = ux_host_stack_transfer_request(t);
    print_xfer(tag, t, st);
    if (st != UX_SUCCESS) return st;

    /* Wait for completion_code to settle (avoid busy re-issuing transfers). */
    UINT cc = wait_transfer_complete(t, t->ux_transfer_request_timeout_value, tag);
    if (cc != t->ux_transfer_request_completion_code && cc != UX_SUCCESS) {
        /* In case we returned a synthesized timeout code, reflect it in logs. */
        t->ux_transfer_request_completion_code = cc;
    }

    /* Best-effort recovery when the endpoint gets stalled. */
#if MSC_AUTO_CLEAR_STALL
#ifdef UX_TRANSFER_STALLED
    if (t->ux_transfer_request_completion_code == UX_TRANSFER_STALLED) {
        printf("[msc] %s: endpoint stalled -> ux_host_stack_endpoint_reset()\r\n", tag);
        (void)ux_host_stack_endpoint_reset(ep);
    }
#endif
#endif

    return t->ux_transfer_request_completion_code;
}

static UINT cbi_adsc(UX_DEVICE *dev, UINT ifnum, UCHAR cmdblk[12])
{
    UX_ENDPOINT *ep0 = &dev->ux_device_control_endpoint;
    UX_TRANSFER *t = &ep0->ux_endpoint_transfer_request;

    t->ux_transfer_request_endpoint = ep0;

    t->ux_transfer_request_type = 0x21;      /* Host->Dev | Class | Interface */
    t->ux_transfer_request_function = 0x00;  /* ADSC */
    t->ux_transfer_request_value = 0;
    t->ux_transfer_request_index = ifnum;

    t->ux_transfer_request_data_pointer = cmdblk;
    t->ux_transfer_request_requested_length = 12;
    t->ux_transfer_request_timeout_value = ms_to_ticks(1000u);

    t->ux_transfer_request_maximum_length = 12;
    t->ux_transfer_request_packet_length  = ep0->ux_endpoint_descriptor.wMaxPacketSize;

    UINT st = ux_host_stack_transfer_request(t);
    print_xfer("ADSC(EP0)", t, st);
    if (st != UX_SUCCESS) return st;
    return t->ux_transfer_request_completion_code;
}

static void ufi_from_cdb6(UCHAR out12[12], UCHAR op, UCHAR b1, UCHAR b2, UCHAR b3, UCHAR b4, UCHAR b5)
{
    memset(out12, 0, 12);
    out12[0] = op; out12[1] = b1; out12[2] = b2; out12[3] = b3; out12[4] = b4; out12[5] = b5;
}

static void ufi_from_cdb10(UCHAR out12[12], const UCHAR cdb10[10])
{
    memset(out12, 0, 12);
    memcpy(out12, cdb10, 10);
}

static UINT poll_bulk_in(UX_ENDPOINT *ep_in, UCHAR *buf, ULONG len, ULONG overall_timeout_ms)
{
    /*
     * Issue ONE Bulk IN and wait for completion up to overall_timeout_ms.
     * (endpoint_xfer_once() itself waits for completion_code to settle.)
     */
    return endpoint_xfer_once(ep_in, buf, len, overall_timeout_ms, "Bulk IN");
}

#if MSC_USE_INT_STATUS
static UINT poll_int_status(UX_ENDPOINT *ep_int, UCHAR st2[2], ULONG overall_timeout_ms)
{
    const ULONG sleep_ticks = ms_to_ticks(MSC_POLL_SLEEP_MS);
    ULONG remaining = overall_timeout_ms;

    while (remaining > 0) {
        UINT st = endpoint_xfer_once(ep_int, st2, 2, 200u, "INT-IN status");
        if (st == UX_SUCCESS) return UX_SUCCESS;

        tx_thread_sleep(sleep_ticks);
        if (remaining > MSC_POLL_SLEEP_MS) remaining -= MSC_POLL_SLEEP_MS;
        else remaining = 0;
    }
    return UX_ERROR;
}
#endif

typedef struct DEV_MSC_UFI_CBI_S {
    UX_DEVICE *dev;
    UX_CONFIGURATION *cfg;
    UX_INTERFACE *itf;
    UINT ifnum;
    UX_ENDPOINT *ep_out;
    UX_ENDPOINT *ep_in;
    UX_ENDPOINT *ep_int;
} DEV_MSC_UFI_CBI;

UINT msc_dev_init(DEV_MSC_UFI_CBI *dev, UX_DEVICE *ux_dev)
{
    memset(dev, 0, sizeof(DEV_MSC_UFI_CBI));
    if (ux_dev == UX_NULL) {
        printf("[msc] no device\r\n");
        return UX_ERROR;
    }

    UINT st;
    dev->dev = ux_dev;
    st = activate_config_and_interface(dev->dev, &dev->cfg, &dev->itf);
    if (st != UX_SUCCESS) {
        printf("[msc] activate_config_and_interface failed: %s(%u)\r\n",
               ux_status_str(st), (unsigned)st);
        return st;
    }

    st = get_cbi_eps(dev->itf, &dev->ep_out, &dev->ep_in, &dev->ep_int);
    if (st != UX_SUCCESS) {
        printf("[msc] endpoints not found\r\n");
        return st;
    }

    dump_ep("Bulk OUT", dev->ep_out);
    dump_ep("Bulk IN ", dev->ep_in);
#if MSC_USE_INT_STATUS
    if (dev->ep_int) dump_ep("Int  IN ", dev->ep_int);
#endif

    dev->ifnum = 0;

    return UX_SUCCESS;
}

static UINT cbi_exec_in(DEV_MSC_UFI_CBI *dev, const UCHAR cmdblk_in[12], UCHAR *data,
                        ULONG data_len, ULONG overall_timeout_ms)
{
    UCHAR cmdblk[12];
    memcpy(cmdblk, cmdblk_in, 12);

    UINT st = cbi_adsc(dev->dev, dev->ifnum, cmdblk);
    if (st != UX_SUCCESS) return st;

    if (data && data_len) {
        st = poll_bulk_in(dev->ep_in, data, data_len, overall_timeout_ms);
        if (st != UX_SUCCESS) return st;
    }

#if MSC_USE_INT_STATUS
    UCHAR st2[2] = {0,0};
    st = poll_int_status(dev->ep_int, st2, overall_timeout_ms);
    if (st != UX_SUCCESS) return st;
    if (st2[0] != 0x00 || st2[1] != 0x00) {
        printf("[msc] CBI status bytes = %02X %02X\r\n", st2[0], st2[1]);
    }
#endif

    return UX_SUCCESS;
}

UINT msc_request_sense(DEV_MSC_UFI_CBI *dev, UCHAR *sense)
{
    UCHAR ufi[12];
    ufi_from_cdb6(ufi, 0x03, 0,0,0, 18, 0);
    UINT st = cbi_exec_in(dev, ufi, sense, 18, 5000u);
    printf("[msc] REQUEST SENSE: %s(%u)\r\n", ux_status_str(st), (unsigned)st);
    return st;
}

UINT msc_inquiry(DEV_MSC_UFI_CBI *dev, UCHAR *inq)
{
    UCHAR ufi[12];
    ufi_from_cdb6(ufi, 0x12, 0,0,0, 36, 0);

    UINT st = cbi_exec_in(dev, ufi, inq, 36, 5000u);
    printf("[msc] INQUIRY: %s(%u)\r\n", ux_status_str(st), (unsigned)st);
    return st;
}

UINT msc_read10(DEV_MSC_UFI_CBI *dev, UINT lba, UINT blocks, UCHAR *buf)
{
    UCHAR ufi[12];
    UCHAR cdb_read10[10] = { 0x28,0x00, 0,0,0,0, 0, 0,1, 0 };
    cdb_read10[1] = ((lba >> 24) & 0xff);
    cdb_read10[2] = ((lba >> 16) & 0xff);
    cdb_read10[3] = ((lba >>  8) & 0xff);
    cdb_read10[4] = ((lba >>  0) & 0xff);
    cdb_read10[7] = ((blocks >> 8) & 0xff);
    cdb_read10[8] = ((blocks >> 0) & 0xff);
    ufi_from_cdb10(ufi, cdb_read10);
    UINT st = cbi_exec_in(dev, ufi, buf, MSC_READ_BLOCK_SIZE * blocks, MSC_TIMEOUT_MS);
    printf("[msc] READ(10) LBA %u: %s(%u)\r\n", lba, ux_status_str(st), (unsigned)st);
    return st;
}

/* ------------------- Main test ------------------- */

void msc_test(void)
{
    UINT st;
    DEV_MSC_UFI_CBI dev;

    printf("\r\n[msc_test] start (CBI/UFI)\r\n");
    st = msc_dev_init(&dev, g_ux_dev);
    if (st != UX_SUCCESS) {
        printf("[msc_test] mas_init() failed\r\n");
        return;
    }

    /* REQUEST SENSE */
    printf("[msc_test] REQUEST SENSE: ...\r\n");
    UCHAR rs[18]; memset(rs, 0, sizeof(rs));
    st = msc_request_sense(&dev, rs);
    if (st == UX_SUCCESS) dump_hex("REQUEST SENSE", rs, sizeof(rs));

    /* INQUIRY */
    printf("[msc_test] INQUIRY: ...\r\n");
    UCHAR inq[36]; memset(inq, 0, sizeof(inq));
    st = msc_inquiry(&dev, inq);
    if (st == UX_SUCCESS) dump_hex("INQUIRY", inq, sizeof(inq));

    /* READ(10) LBA0, 1 block */
    printf("[msc_test] READ(10) ...\r\n");
    static UCHAR buf[MSC_READ_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    st = msc_read10(&dev, 0, 1, buf);
    if (st == UX_SUCCESS) dump_hex("SECTOR", buf, MSC_READ_BLOCK_SIZE);
    st = msc_read10(&dev, 1, 1, buf);
    if (st == UX_SUCCESS) dump_hex("SECTOR", buf, MSC_READ_BLOCK_SIZE);
    msc_read10(&dev, 200, 1, buf);
    msc_read10(&dev, 500, 1, buf);
    msc_read10(&dev, 2, 1, buf);
    msc_read10(&dev, 100, 1, buf);
    msc_read10(&dev, 10, 1, buf);

    printf("[msc_test] done\r\n");
}
