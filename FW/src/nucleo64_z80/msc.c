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

#define MSC_BLOCK_SIZE 512u
#define MSC_TIMEOUT_MS 5000u
#define MSC_BULK_TRY_TIMEOUT_MS 500u
#define MSC_POLL_SLEEP_MS 50u
#define MSC_AUTO_CLEAR_STALL 1 // Auto-recover from STALL by resetting the endpoint (best-effort)
#define MSC_USE_INT_STATUS 1  // 0: skip INT-IN status phase (disabled for now)

#define BLOCKING_FACTOR 1
#define INVALID_LBA 0xffffffffu
static UCHAR g_cache_buf[MSC_BLOCK_SIZE * BLOCKING_FACTOR];
static uint32_t g_cache_lba = INVALID_LBA;
static bool g_dev_prepared = false;

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

__attribute__ ((unused)) static void dump_ep(const char *name, UX_ENDPOINT *ep)
{
    const UX_ENDPOINT_DESCRIPTOR *d = &ep->ux_endpoint_descriptor;
    printf("[msc] %s: addr=0x%02X attr=0x%02X maxpkt=%u interval=%u\r\n",
           name,
           (unsigned)d->bEndpointAddress,
           (unsigned)d->bmAttributes,
           (unsigned)d->wMaxPacketSize,
           (unsigned)d->bInterval);
}

static const char *scsi_sense_key_str(UCHAR key)
{
    switch (key & 0x0F) {
    case 0x0: return "NO SENSE";
    case 0x1: return "RECOVERED ERROR";
    case 0x2: return "NOT READY";
    case 0x3: return "MEDIUM ERROR";
    case 0x4: return "HARDWARE ERROR";
    case 0x5: return "ILLEGAL REQUEST";
    case 0x6: return "UNIT ATTENTION";
    case 0x7: return "DATA PROTECT";
    case 0x8: return "BLANK CHECK";
    case 0x9: return "VENDOR SPECIFIC";
    case 0xA: return "COPY ABORTED";
    case 0xB: return "ABORTED COMMAND";
    case 0xC: return "EQUAL";
    case 0xD: return "VOLUME OVERFLOW";
    case 0xE: return "MISCOMPARE";
    default:  return "SENSE?(unknown)";
    }
}

static void scsi_dump_request_sense_fixed18(const char *tag, const UCHAR rs[18])
{
    /* SPC fixed format sense: rs[2]=SenseKey, rs[12]=ASC, rs[13]=ASCQ */
    UCHAR resp_code = rs[0] & 0x7F;
    UCHAR sk        = rs[2] & 0x0F;
    UCHAR asc       = rs[12];
    UCHAR ascq      = rs[13];

    printf("[msc] %s: RS resp=0x%02X SK=0x%X(%s) ASC/ASCQ=%02X/%02X\r\n",
           tag, (unsigned)resp_code, (unsigned)sk, scsi_sense_key_str(sk),
           (unsigned)asc, (unsigned)ascq);

    /* Keep the full bytes too (helps when device returns vendor fields). */
    // dump_hex("REQUEST SENSE (raw)", rs, 18);
}

static bool scsi_is_no_medium_3a00(const UCHAR rs[18])
{
    UCHAR sk   = rs[2] & 0x0F;
    UCHAR asc  = rs[12];
    UCHAR ascq = rs[13];
    return (sk == 0x02) && (asc == 0x3A) && (ascq == 0x00);
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
        if (last_cc != 0xFFFFFFFFu && cc != last_cc) {
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
    if (st != UX_SUCCESS) {
        print_xfer(tag, t, st);
        return st;
    }

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
    if (st != UX_SUCCESS) {
        char tmp[20];
        sprintf(tmp, "ADSC(EP0): cmd%x", cmdblk[0]);
        print_xfer(tmp, t, st);
        return st;
    }
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

    // dump_ep("Bulk OUT", dev->ep_out);
    // dump_ep("Bulk IN ", dev->ep_in);
#if MSC_USE_INT_STATUS
    // if (dev->ep_int) dump_ep("Int  IN ", dev->ep_int);
#endif

    dev->ifnum = 0;

    return UX_SUCCESS;
}

__attribute__ ((unused)) static UINT msc_dev_soft_reset(DEV_MSC_UFI_CBI *dev)
{
    /* Best-effort recovery: re-activate config/interface and re-discover endpoints.
     * This helps some CBI/UFI devices recover after media-absent or stalled states.
     */
    if (dev == UX_NULL || dev->dev == UX_NULL) return UX_ERROR;

    UINT st = activate_config_and_interface(dev->dev, &dev->cfg, &dev->itf);
    if (st != UX_SUCCESS) return st;

    st = get_cbi_eps(dev->itf, &dev->ep_out, &dev->ep_in, &dev->ep_int);
    return st;
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

UINT msc_test_unit_ready(DEV_MSC_UFI_CBI *dev, ULONG timeout_ms)
{
    UCHAR ufi[12];
    ufi_from_cdb6(ufi, 0x00, 0,0,0, 0, 0); /* TEST UNIT READY */
    return cbi_exec_in(dev, ufi, NULL, 0, timeout_ms);
}

UINT msc_request_sense(DEV_MSC_UFI_CBI *dev, UCHAR *sense)
{
    UCHAR ufi[12];
    ufi_from_cdb6(ufi, 0x03, 0,0,0, 18, 0);
    UINT st = cbi_exec_in(dev, ufi, sense, 18, 5000u);
    if (st != UX_SUCCESS) {
        printf("[msc] REQUEST SENSE: %s(%u)\r\n", ux_status_str(st), (unsigned)st);
    }
    return st;
}

UINT msc_inquiry(DEV_MSC_UFI_CBI *dev, UCHAR *inq)
{
    UCHAR ufi[12];
    ufi_from_cdb6(ufi, 0x12, 0,0,0, 36, 0);

    UINT st = cbi_exec_in(dev, ufi, inq, 36, 5000u);
    if (st != UX_SUCCESS) {
        printf("[msc] INQUIRY: %s(%u)\r\n", ux_status_str(st), (unsigned)st);
    }
    return st;
}

UINT msc_read10(DEV_MSC_UFI_CBI *dev, UINT lba, UINT blocks, UCHAR *buf)
{
    UCHAR ufi[12];
    UCHAR cdb_read10[10] = { 0x28,0x00, 0,0,0,0, 0, 0,1, 0 };
    cdb_read10[2] = ((lba >> 24) & 0xff);
    cdb_read10[3] = ((lba >> 16) & 0xff);
    cdb_read10[4] = ((lba >>  8) & 0xff);
    cdb_read10[5] = ((lba >>  0) & 0xff);
    cdb_read10[7] = ((blocks >> 8) & 0xff);
    cdb_read10[8] = ((blocks >> 0) & 0xff);
    // dump_hex("msc_read10", cdb_read10, sizeof(cdb_read10));
    ufi_from_cdb10(ufi, cdb_read10);
    UINT st = cbi_exec_in(dev, ufi, buf, MSC_BLOCK_SIZE * blocks, MSC_TIMEOUT_MS);
    if (st !=  UX_SUCCESS) {
        printf("[msc] READ(10) LBA %u: %s(%u)\r\n", lba, ux_status_str(st), (unsigned)st);
    }
    return st;
}

DEV_MSC_UFI_CBI g_dev = { NULL };

void disk_msc_init(void) {
    // nothing to do
}

bool disk_msc_have_boot_disk(void) {
    return false;
}

static bool disk_msc_prepair(uint32_t offs) {
    UINT st;
    uint32_t lba = (offs / MSC_BLOCK_SIZE / BLOCKING_FACTOR) * BLOCKING_FACTOR;
    if (g_ux_dev == NULL) {
        printf("%s: no device\r\n", __func__);
        g_cache_lba = INVALID_LBA;
        return false;
    }
    if (g_dev.dev != g_ux_dev) {
        g_cache_lba = INVALID_LBA;
        g_dev_prepared = false;
        st = msc_dev_init(&g_dev, g_ux_dev);
        if (st != UX_SUCCESS) {
            printf("%s: no device\r\n", __func__);
            return false;
        }
    }

    /* TEST UNIT READY */
    if (msc_test_unit_ready(&g_dev, 300u) != UX_SUCCESS) {
        g_cache_lba = INVALID_LBA;
        g_dev_prepared = false;
    }

    if (g_cache_lba == lba) return true;
    if (g_dev_prepared)  return true;

    /* INQUIRY (device identification) */
    UCHAR inq[36];
    memset(inq, 0, sizeof(inq));
    st = msc_inquiry(&g_dev, inq);

    /* REQUEST SENSE (optional, but useful for initial state) */
    UCHAR rs[18]; memset(rs, 0, sizeof(rs));
    st = msc_request_sense(&g_dev, rs);
    if (st != UX_SUCCESS) {
        printf("%s: REQUEST_SENSE failed\r\n", __func__);
        return false;
    }
    if (scsi_is_no_medium_3a00(rs)) {
        /* No medium present.
         * Intentionally fail silently to avoid flooding errors while media is absent.
         */
        return false;
    }
    // scsi_dump_request_sense_fixed18("initial", rs);

    /* INQUIRY (device identification) */
    memset(inq, 0, sizeof(inq));
    st = msc_inquiry(&g_dev, inq);
    if (st != UX_SUCCESS) {
        printf("%s: INQUIRY failed\r\n", __func__);
        return false;
    }
    g_dev_prepared = true;

    return true;
}

char *offs_str(uint32_t offs, char *buf, size_t len) {
    uint32_t lba = (offs / MSC_BLOCK_SIZE);
    uint32_t lba_block = (lba / BLOCKING_FACTOR) * BLOCKING_FACTOR;
    snprintf(buf, len, "0x%lx(%lu+%lu+%lu)", offs, lba_block, lba % BLOCKING_FACTOR,
            offs - (lba * MSC_BLOCK_SIZE));
    return buf;
}

static bool disk_msc_fill(uint8_t drive, uint32_t offs) {
    uint32_t lba = (offs / MSC_BLOCK_SIZE);
    uint32_t lba_block = (lba / BLOCKING_FACTOR) * BLOCKING_FACTOR;
    if (g_cache_lba == lba_block) return true;

    /* READ(10) with a small retry loop.
     * - On failure, print REQUEST SENSE (decoded) to see why.
     * - If the endpoint got stalled, endpoint_xfer_once() already did a best-effort reset.
     */
    UINT st = UX_ERROR;
    for (unsigned attempt = 0; attempt < 6; ++attempt) {
        if (!disk_msc_prepair(offs)) {
            /*
             * Ignore error and attempt to read anyway...
             * the reading may recover the drive status some how
             */
            //(void)msc_dev_soft_reset(&g_dev);
            //continue;
        }
        g_cache_lba = INVALID_LBA;

        st = msc_read10(&g_dev, lba_block, BLOCKING_FACTOR, g_cache_buf);
        if (st == UX_SUCCESS) {
            g_cache_lba = lba_block;
            return true;
        }

        UCHAR rs[18]; memset(rs, 0, sizeof(rs));
        UINT st_rs = msc_request_sense(&g_dev, rs);
        if (st_rs == UX_SUCCESS) {
            scsi_dump_request_sense_fixed18("READ(10) failed", rs);
        } else {
            printf("[msc] REQUEST SENSE after READ(10) failed: %s(%u)\r\n",
                   ux_status_str(st_rs), (unsigned)st_rs);
        }

        /* Short backoff. 1st retry is usually enough when it's transient. */
        tx_thread_sleep(ms_to_ticks(50u));

#if 0
        /* Best-effort recovery: some CBI/UFI devices get stuck after errors
         * (e.g. media-absent access). Reinitialize the interface/endpoints once,
         * then retry the READ(10) once.
         */
        printf("[msc] attempt to reset...\r\n");
        (void)msc_dev_soft_reset(&g_dev);
        st = msc_read10(&g_dev, lba_block, BLOCKING_FACTOR, g_cache_buf);
        if (st == UX_SUCCESS) {
            g_cache_lba = lba_block;
            return true;
        }
        tx_thread_sleep(ms_to_ticks(50u));
#endif
    }

    char tmp[80];
    printf("%s: %s: read failed\r\n", __func__, offs_str(offs, tmp, sizeof(tmp)));
    return false;
}

static bool disk_msc_writeback(uint8_t drive, uint32_t offs) {
    //uint32_t lba = (offs / MSC_BLOCK_SIZE);
    //uint32_t lba_block = (lba / BLOCKING_FACTOR) * BLOCKING_FACTOR;

    // not implemented yet
    // UINT st = msc_write10(&g_dev, lba, 1, &g_cache_buf[(lba - lba_block) * MSC_BLOCK_SIZE]);
    // if (st == UX_SUCCESS) return true;

    char tmp[80];
    printf("%s: %s: write failed\r\n", __func__, offs_str(offs, tmp, sizeof(tmp)));
    return false;
}

static uint32_t offs_in_cache(uint32_t offs) {
    uint32_t lba = (offs / MSC_BLOCK_SIZE);
    uint32_t lba_block = (lba / BLOCKING_FACTOR) * BLOCKING_FACTOR;
    return offs - (lba_block * MSC_BLOCK_SIZE);
}

bool disk_msc_read(uint8_t drive, uint32_t offs, uint8_t *buf, int buf_len) {
    if (MSC_BLOCK_SIZE < offs % MSC_BLOCK_SIZE + buf_len) {
        printf("%s: invalid parameter: offs=%lu, len=%d\r\n", __func__, offs, buf_len);
        return false;
    }
    if (!disk_msc_fill(drive, offs)) return false;
    memcpy(buf, &g_cache_buf[offs_in_cache(offs)], buf_len);
    return true;
}

bool disk_msc_write(uint8_t drive, uint32_t offs, uint8_t *buf, int buf_len) {
    if (MSC_BLOCK_SIZE < offs % MSC_BLOCK_SIZE + buf_len) {
        printf("%s: invalid parameter: offs=%lu, len=%d\r\n", __func__, offs, buf_len);
        return false;
    }
    if (!disk_msc_fill(drive, offs)) return false;
    memcpy(&g_cache_buf[offs_in_cache(offs)], buf, buf_len);
    if (!disk_msc_writeback(drive, offs)) return false;
    return false;
}

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
    static UCHAR buf[MSC_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    st = msc_read10(&dev, 0, 1, buf);
    if (st == UX_SUCCESS) dump_hex("SECTOR", buf, MSC_BLOCK_SIZE);
    st = msc_read10(&dev, 1, 1, buf);
    if (st == UX_SUCCESS) dump_hex("SECTOR", buf, MSC_BLOCK_SIZE);
    msc_read10(&dev, 200, 1, buf);
    msc_read10(&dev, 500, 1, buf);
    msc_read10(&dev, 2, 1, buf);
    msc_read10(&dev, 100, 1, buf);
    msc_read10(&dev, 10, 1, buf);

    printf("[msc_test] done\r\n");
}
