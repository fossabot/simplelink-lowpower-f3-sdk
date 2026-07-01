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
 * @file  zb_mux.h
 * @brief Zigbee OSIF serial transport replacement for the embedded TI Combined
 *        Serial MUX.
 *
 * This module replaces ti_f3_serial.c in the Zigbee build.  Instead of
 * writing Zigbee MAC frames to a dedicated UART, it routes them through the
 * Combined Serial MUX task so that both BLE HCI and Zigbee MAC traffic share
 * a single UART link to the Linux host.
 *
 * Data flow
 * ---------
 *
 *   Zigbee stack  →  zb_osif_serial_put_bytes()
 *       │
 *       ▼  MuxTask_sendPacket(MUX_NLI_ZB, ...)
 *   MUX TX queue  →  HDLC/Spinel encode  →  UART TX  →  Linux host
 *
 *   Linux host  →  UART RX  →  HDLC/Spinel decode  →  NLI = ZB
 *       │
 *       ▼  ZbMux_muxRxCb(buf, len)  [MUX task context]
 *   SER_CTX().byte_received_cb(byte)  [once per byte]
 *       │
 *       ▼
 *   Zigbee stack (processes inbound MAC frame byte-by-byte)
 *
 * Usage
 * -----
 *   1. Exclude ti_f3_serial.c from the build (duplicate symbols).
 *   2. Call ZbMux_init() once from main() BEFORE MuxTask_create().
 *   3. Call MuxTask_create() and vTaskStartScheduler() as usual.
 *
 * Thread safety
 * -------------
 *   zb_osif_serial_put_bytes() may be called from the Zigbee task context.
 *   ZbMux_muxRxCb() is called from the MUX task context.
 *   Both call into thread-safe APIs (MuxTask_sendPacket uses a queue;
 *   byte_received_cb is called with the Zigbee stack synchronisation intact).
 */

#ifndef ZB_MUX_H
#define ZB_MUX_H

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------------*/

/*!
 * @brief Initialise the Zigbee↔MUX transport bridge.
 *
 * Registers ZbMux_muxRxCb() with the MUX task via
 * MuxTask_registerRxCb(MUX_NLI_ZB, ...) so that inbound Zigbee frames
 * received from the Linux host are forwarded to the Zigbee stack one byte
 * at a time via the registered byte_received_cb.
 *
 * Must be called once from main() before MuxTask_create().
 */
void ZbMux_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ZB_MUX_H */
