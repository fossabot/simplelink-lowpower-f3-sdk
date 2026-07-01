/*
 * Copyright (c) 2025, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Texas Instruments Incorporated nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*!
 * @file  zb_mux.c
 * @brief Zigbee OSIF serial transport replacement for the embedded TI Combined
 *        Serial MUX.
 *
 * This file is a drop-in replacement for ti_f3_serial.c.  It provides the
 * same Zigbee OSIF serial symbols but routes traffic through the Combined
 * Serial MUX task rather than a dedicated UART instance.
 *
 * See zb_mux.h for the architecture overview and data flow diagram.
 *
 * Assert policy:
 *   Programming errors (NULL pointers, payload too large for the MUX queue)
 *   are asserted.  The Zigbee MAC layer limits frames to 127 bytes, well
 *   within MUX_MSG_BUF_LEN (512 bytes), so an oversized assert fires only
 *   on a serious programming mistake.
 */

#include "zb_mux.h"
#include "mux_task_app.h"

#include <assert.h>
#include <stdint.h>

/* Zigbee OSIF — provides zb_serial_ctx_t, SER_CTX(), zb_uint8_t, etc. */
#include "zb_common.h"

/*---------------------------------------------------------------------------
 * Forward declaration (only needed when the serial guard is active)
 *--------------------------------------------------------------------------*/

#if defined ZB_HAVE_SERIAL || defined USE_ASSERT
static void ZbMux_muxRxCb(const uint8_t *buf, uint16_t len);
#endif

/*---------------------------------------------------------------------------
 * ZbMux_init
 *
 * Intentionally placed OUTSIDE the ZB_HAVE_SERIAL guard so that it can
 * always be called from main() regardless of whether the Zigbee serial
 * transport is enabled in the current build configuration.
 * When neither ZB_HAVE_SERIAL nor USE_ASSERT is defined, the function is
 * a no-op but the symbol is always present for the linker.
 *--------------------------------------------------------------------------*/

void ZbMux_init(void)
{
#if defined ZB_HAVE_SERIAL || defined USE_ASSERT
    /*
     * Register the MUX RX callback for the Zigbee NLI channel.
     *
     * When the MUX task receives an inbound packet with NLI = MUX_NLI_ZB
     * it will call ZbMux_muxRxCb() with the decoded payload.
     *
     * Must be called before MuxTask_create() — see mux_task_app.h contract.
     */
    MuxTask_registerRxCb((uint8_t)MUX_NLI_ZB, ZbMux_muxRxCb);
#endif /* ZB_HAVE_SERIAL || USE_ASSERT */
}

/*
 * Guard matches ti_f3_serial.c so that the remaining OSIF symbols compile
 * under the same conditions and this file is a safe swap-in.
 */
#if defined ZB_HAVE_SERIAL || defined USE_ASSERT

/*---------------------------------------------------------------------------
 * Module-private state
 *--------------------------------------------------------------------------*/

/*!
 * Global serial context instance.
 *
 * Declared extern in zb_hal_serial.h and referenced via the SER_CTX() macro
 * throughout the Zigbee SDK.  The definition was previously in ti_f3_serial.c;
 * it lives here now that we have replaced that file.
 *
 * Only the .byte_received_cb field is used by this module.  The TX ring
 * buffer fields (p_tx_buf, tx_buf, tx_buf_cap, tx_in_progress) are not used
 * because transmit goes directly through MuxTask_sendPacket().
 */
zb_serial_ctx_t zb_serial_ctx;

/*---------------------------------------------------------------------------
 * Zigbee OSIF serial API  (replaces ti_f3_serial.c)
 *--------------------------------------------------------------------------*/

/*!
 * @brief Initialise the Zigbee serial transport.
 *
 * In the MUX configuration the UART is owned entirely by mux_uart.c.
 * This function is a no-op: no UART instance is opened here.
 * ZbMux_init() handles MUX registration and must be called from main()
 * before MuxTask_create().
 */
void zb_osif_serial_init(void)
{
    /* No UART to open — MUX owns the UART via mux_uart.c */
}

/*!
 * @brief De-initialise the Zigbee serial transport.
 *
 * No-op: the MUX UART is closed by MuxUart_close() if needed.
 */
void zb_osif_serial_deinit(void)
{
    /* No UART to close — MUX owns the UART via mux_uart.c */
}

#ifdef ZB_MACSPLIT_TRANSPORT_SERIAL
/*!
 * @brief Initialise the Zigbee NCP serial transport.
 *
 * Delegates to zb_osif_serial_init() to match the ti_f3_serial.c pattern.
 */
void zb_osif_serial_transport_init(void)
{
    zb_osif_serial_init();
}
#endif /* ZB_MACSPLIT_TRANSPORT_SERIAL */

/*!
 * @brief Send bytes from the Zigbee stack to the Linux host.
 *
 * Called by the Zigbee MAC layer when it has a frame to transmit.
 * The payload is enqueued in the MUX TX queue (copied by value).
 *
 * IEEE 802.15.4 MAC frames are at most 127 bytes; MUX_MSG_BUF_LEN is 512
 * bytes, so a single call should never exceed the queue message size.
 *
 * @param buf  Pointer to the Zigbee MAC frame bytes.
 * @param len  Number of bytes to transmit (must be > 0).
 */
void zb_osif_serial_put_bytes(const zb_uint8_t *buf, zb_short_t len)
{
    MuxErr_t err;

    assert(buf != NULL);                        /* NULL payload pointer          */
    assert(len > 0);                            /* zero-length transmit is a bug */
    assert((uint16_t)len <= MUX_MSG_BUF_LEN);  /* payload exceeds queue slot    */

    if (buf == NULL || len <= 0 || (uint16_t)len > MUX_MSG_BUF_LEN)
    {
        return;
    }

    err = MuxTask_sendPacket((uint8_t)MUX_NLI_ZB, buf, (uint16_t)len);

    assert(err == MUX_SUCCESS);   /* MUX TX queue full or invalid params */
    (void)err;
}

#ifdef ZB_MACSPLIT_TRANSPORT_SERIAL
/*!
 * @brief NCP transport TX wrapper — delegates to zb_osif_serial_put_bytes().
 */
void zb_osif_serial_transport_put_bytes(zb_uint8_t *buf, zb_short_t len)
{
    zb_osif_serial_put_bytes(buf, len);
}
#endif /* ZB_MACSPLIT_TRANSPORT_SERIAL */

/*!
 * @brief Register the Zigbee per-byte RX callback.
 *
 * The Zigbee stack calls this once during initialisation to provide the
 * function that should be called for each received byte.
 * Stored in SER_CTX().byte_received_cb; invoked from ZbMux_muxRxCb().
 *
 * @param hnd  Callback function pointer; NULL to deregister.
 */
void zb_osif_set_uart_byte_received_cb(void (*hnd)(zb_uint8_t))
{
    SER_CTX().byte_received_cb = hnd;
}

/*!
 * @brief Override the Zigbee TX ring buffer (no-op in MUX configuration).
 *
 * The TX ring buffer is used by ti_f3_serial.c to stage bytes before the
 * UART write callback drains them.  In the MUX configuration all transmit
 * goes through MuxTask_sendPacket(), so this ring buffer is not used.
 */
void zb_osif_set_user_io_buffer(zb_byte_array_t *buf_ptr, zb_ushort_t capacity)
{
    (void)buf_ptr;
    (void)capacity;
    /* TX ring buffer not used — MUX owns transmit path */
}

/*!
 * @brief Suspend the Zigbee serial transport for low-power sleep (no-op).
 *
 * In the MUX configuration the UART is owned by mux_uart.c.
 * Power management of the shared UART is handled at the MUX level.
 */
void zb_osif_uart_sleep(void)
{
    /* MUX owns the UART — power management handled at MUX level */
}

/*!
 * @brief Resume the Zigbee serial transport after low-power sleep (no-op).
 *
 * See zb_osif_uart_sleep().
 */
void zb_osif_uart_wake_up(void)
{
    /* MUX owns the UART — power management handled at MUX level */
}

/*---------------------------------------------------------------------------
 * ZbMux_muxRxCb  (private)
 *
 * Called by the MUX task when an inbound packet with NLI = MUX_NLI_ZB has
 * been received from the Linux host.  The payload is a Zigbee MAC frame.
 *
 * The Zigbee OSIF serial layer expects bytes to arrive one at a time via
 * byte_received_cb().  We iterate over the decoded buffer and call the
 * callback once per byte to match that contract exactly.
 *
 * @param buf  Pointer to the decoded Zigbee MAC payload (valid for this call).
 * @param len  Number of bytes in the payload.
 *--------------------------------------------------------------------------*/

static void ZbMux_muxRxCb(const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    assert(buf != NULL);   /* MUX task passed NULL — internal logic error */
    assert(len != 0U);     /* zero-length Zigbee packet — should not happen */

    if (buf == NULL || len == 0U)
    {
        return;
    }

    if (SER_CTX().byte_received_cb == NULL)
    {
        /*
         * Zigbee stack has not yet registered its byte callback.
         * This can happen if a frame arrives before the stack has called
         * zb_osif_set_uart_byte_received_cb().  Silently drop — not asserted
         * because it is a transient race condition at startup.
         */
        return;
    }

    for (i = 0U; i < len; i++)
    {
        SER_CTX().byte_received_cb((zb_uint8_t)buf[i]);
    }
}

#endif /* ZB_HAVE_SERIAL || USE_ASSERT */
