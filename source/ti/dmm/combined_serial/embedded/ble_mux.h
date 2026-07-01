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
 * @file  ble_mux.h
 * @brief BLE HCI transport glue for the embedded TI Combined Serial MUX.
 *
 * This module wires the BLE HCI transport layer into the MUX task so that
 * HCI packets flow between the CC2755P BLE controller and the Linux host
 * over the Combined Serial UART link.
 *
 * Data flow
 * ---------
 *
 *   BLE controller (HCI events / ACL data)
 *       │
 *       ▼  hciControllerToHostCallbacks_t.send()  [BLE stack context]
 *   BleMux_hciSendCb()
 *       │
 *       ▼  MuxTask_sendPacket(MUX_NLI_BLE, ...)
 *   MUX TX queue  →  HDLC/Spinel encode  →  UART TX  →  Linux host
 *
 *   Linux host  →  UART RX  →  HDLC/Spinel decode  →  NLI = BLE
 *       │
 *       ▼  gRxCbs[MUX_NLI_BLE](buf, len)  [MUX task context]
 *   BleMux_muxRxCb()
 *       │
 *       ▼  HCI_HostToControllerSend()
 *   BLE controller (processes inbound HCI command / ACL data)
 *
 * Usage
 * -----
 *   1. Call BleMux_init() once from main() BEFORE MuxTask_create().
 *      This registers both the HCI TX callback and the MUX RX callback.
 *   2. Call MuxTask_create() and vTaskStartScheduler() as usual.
 *
 * Thread safety
 * -------------
 *   BleMux_hciSendCb() is called from BLE stack task context (OSAL).
 *   BleMux_muxRxCb() is called from the MUX task context.
 *   Both call into thread-safe APIs (MuxTask_sendPacket uses a queue;
 *   HCI_HostToControllerSend is protected by the BLE stack internally).
 */

#ifndef BLE_MUX_H
#define BLE_MUX_H

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------------*/

/*!
 * @brief Initialise the BLE↔MUX transport bridge.
 *
 * Performs in order:
 *   1. Registers BleMux_hciSendCb() with the BLE HCI layer via
 *      HCI_ControllerToHostRegisterCb().  This callback is invoked by the
 *      BLE stack whenever it generates an HCI event or ACL data packet.
 *   2. Registers BleMux_muxRxCb() with the MUX task via
 *      MuxTask_registerRxCb(MUX_NLI_BLE, ...).  This callback is invoked
 *      by the MUX task when a BLE-NLI packet arrives from the Linux host.
 *
 * Must be called once from main() before MuxTask_create().
 */
void BleMux_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_MUX_H */
