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
 * @file  mux_task_app.h
 * @brief FreeRTOS MUX task for the embedded TI Combined Serial Interface.
 *
 * The MUX task is the single owner of the UART.  It:
 *   - Encodes outbound packets (from BLE/Zigbee stacks) as Spinel+HDLC frames
 *     and writes them to the UART.
 *   - Decodes inbound HDLC frames from the UART, extracts the NLI, and
 *     delivers the payload to the registered per-stack RX callback.
 *   - Sends periodic CMD_KEEPALIVE frames to the Linux host and processes
 *     CMD_KEEPALIVE_ACK replies.
 *
 * Data flow overview
 * ------------------
 *
 *   BLE task / Zigbee task
 *       │
 *       ▼  MuxTask_sendPacket(nli, buf, len)  [any task context]
 *   TX queue (MuxQueueMsg_t × MUX_QUEUE_DEPTH)
 *       │
 *       ▼  xTaskNotify(TX_BIT) wakes MUX task
 *   MuxSpinelHdlc_encode()  →  MuxUart_write()  →  UART TX pin → host
 *
 *   UART RX pin → ISR → MuxBuf_write() → ring buffer
 *       │
 *       ▼  xTaskNotifyFromISR(RX_BIT), MUX task wakes (xTaskNotifyWait)
 *   MuxBuf_extractFrame() → MuxHdlc_decode() → MuxSpinel_parseFrame()
 *       │
 *       ├── NLI = BLE  →  gRxCbs[MUX_NLI_BLE](payload, len)
 *       ├── NLI = ZB   →  gRxCbs[MUX_NLI_ZB](payload, len)
 *       └── NLI = KA   →  keepalive ACK handling (no callback)
 *
 * Task priority and scheduling
 * ----------------------------
 * The MUX task runs at priority MUX_TASK_PRIORITY (7), higher than both
 * the BLE task (6) and the Zigbee task (1).  This ensures that received
 * data is dispatched to the stacks promptly after the UART ISR fires.
 *
 * Blocking strategy — FreeRTOS Task Notifications
 * -------------------------------------------------
 * The MUX task blocks on xTaskNotifyWait() using two notification bits:
 *   MUX_NOTIFY_TX_BIT — set by MuxTask_sendPacket() after each enqueue.
 *   MUX_NOTIFY_RX_BIT — set by the UART ISR via xTaskNotifyFromISR().
 * Both bits are cleared atomically on exit from xTaskNotifyWait().  If
 * multiple TX messages or RX chunks arrive while the task is processing,
 * the bits coalesce and the task drains all queued messages in one pass.
 * On timeout it sends the periodic keepalive.
 *
 * Keepalive
 * ---------
 * The embedded device sends CMD_KEEPALIVE (15555) to the host every
 * MUX_KEEPALIVE_PERIOD_MS milliseconds.  The host is expected to reply with
 * CMD_KEEPALIVE_ACK (15556).  The current implementation logs the ACK;
 * dead-host detection and recovery are left for a future phase.
 *
 * Thread safety
 * -------------
 * - MuxTask_registerRxCb() must be called before MuxTask_create().
 * - MuxTask_sendPacket() is thread-safe and may be called from any task.
 * - Large buffers (ring buffer, frame staging) are module-static and
 *   accessed only by the MUX task or under HwiP critical sections.
 */

#ifndef MUX_TASK_APP_H
#define MUX_TASK_APP_H

#include "../mux_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * Task configuration
 *--------------------------------------------------------------------------*/

/*!
 * FreeRTOS task priority for the MUX task.
 * Must be higher than BLE (typically 6) and Zigbee (1) so that received
 * data is dispatched promptly.
 */
#define MUX_TASK_PRIORITY       (7U)

/*!
 * MUX task stack size in bytes.
 * Large intermediate buffers (ring buffer, HDLC frame, decoded Spinel frame,
 * encoded TX frame) are all module-static, so the task stack only needs to
 * accommodate function call frames.
 */
#define MUX_TASK_STACK_BYTES    (1024U)

/*!
 * Interval between CMD_KEEPALIVE transmissions (milliseconds).
 * Also used as the xQueueSelectFromSet() timeout — the task wakes at least
 * this often even when idle.
 */
#define MUX_KEEPALIVE_PERIOD_MS (5000U)

/*---------------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------------*/

/*!
 * @brief Register a per-NLI RX callback.
 *
 * May be called before or after MuxTask_create().  The callback is invoked
 * by the MUX task when a decoded inbound packet arrives for the given NLI
 * channel.
 *
 * @param nli  NLI channel (MUX_NLI_BLE or MUX_NLI_ZB).
 * @param cb   Callback function; NULL to deregister.
 */
void MuxTask_registerRxCb(uint8_t nli, MuxStackRxCb_t cb);

/*!
 * @brief Initialise MUX resources and create the FreeRTOS MUX task.
 *
 * Performs in order:
 *   1. Creates the TX queue.
 *   2. Initialises the intermediary RX ring buffer.
 *   3. Creates the MUX FreeRTOS task at MUX_TASK_PRIORITY (does not run
 *      until vTaskStartScheduler() is called).
 *   4. Opens the UART via MuxUart_open(), passing the task handle so the
 *      UART ISR can wake the task via xTaskNotifyFromISR().
 *
 * Any OS resource allocation failure triggers a configASSERT() halt.
 * Call this function once from application main() before vTaskStartScheduler().
 * MUX clients (BLE HCI, Zigbee) must NOT call this — they only register
 * their RX callbacks via MuxTask_registerRxCb().
 *
 * @param uartIndex  UART2 instance index from SysConfig (CONFIG_MUX_UART).
 * @param baudRate   Baud rate in bits/second (e.g. MUX_UART_DEFAULT_BAUD).
 *
 * @return MUX_SUCCESS   Task created and UART open.
 * @return MUX_ERR_UART  UART2_open() or initial UART2_read() failed.
 */
MuxErr_t MuxTask_create(uint8_t uartIndex, uint32_t baudRate);

/*!
 * @brief Enqueue a packet for transmission to the Linux host.
 *
 * Thread-safe: may be called from any task context (BLE task, Zigbee task).
 * The payload is copied into the TX queue so the caller's buffer is not
 * referenced after this function returns.
 *
 * The MUX task dequeues the message, prepends the Spinel header, HDLC-frames
 * the result, and writes it to the UART.
 *
 * @param nli  NLI channel (MUX_NLI_BLE or MUX_NLI_ZB).
 * @param buf  Payload bytes to transmit (BLE HCI packet or Zigbee MAC frame).
 * @param len  Payload length.  Must be in [1, MUX_MSG_BUF_LEN].
 *
 * @return MUX_SUCCESS      Message enqueued successfully.
 * @return MUX_ERR_INVALID  NULL pointer, zero length, length > MUX_MSG_BUF_LEN,
 *                          or NLI is out of range.
 * @return MUX_ERR_QUEUE    TX queue is full (host or UART not keeping up).
 */
MuxErr_t MuxTask_sendPacket(uint8_t nli, const uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* MUX_TASK_APP_H */
