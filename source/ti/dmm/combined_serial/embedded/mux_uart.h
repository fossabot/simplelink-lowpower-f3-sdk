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
 * @file  mux_uart.h
 * @brief UART2 driver integration for the embedded TI Combined Serial MUX.
 *
 * This module owns the UART2 handle used by the MUX layer.  It provides:
 *   - A blocking TX path for the MUX task to write encoded HDLC frames.
 *   - A callback-driven RX path that feeds received bytes into the shared
 *     intermediary ring buffer and signals the MUX task.
 *
 * Architecture
 * ------------
 *
 *   MUX task (TX)
 *       │
 *       ▼  MuxUart_write(buf, len)
 *   UART2_write() — blocking, called only from the MUX task
 *       │
 *       ▼  wire (UART TX pin)
 *
 *   wire (UART RX pin)
 *       │
 *       ▼  DMA → driver internal ring buffer
 *   muxUart_rxCallback()  ← called from UART ISR
 *       │
 *       ▼  HwiP_disable/restore  (critical section for ring buffer)
 *   MuxBuf_write(rxBuf, ...)   (intermediary ring buffer)
 *       │
 *       ▼  xTaskNotifyFromISR(muxTask, rxNotifyBit, eSetBits)
 *   MUX task wakes (xTaskNotifyFromISR → xTaskNotifyWait), calls MuxBuf_extractFrame()
 *
 * SysConfig note
 * --------------
 * The MUX UART must be configured as a **separate** UART2 instance in
 * SysConfig with both TX and RX pins assigned.  Pass the generated index
 * (e.g. CONFIG_MUX_UART) to MuxUart_open().
 *
 * The existing CONFIG_DISPLAY_UART instance (TX-only, display/debug) is
 * unrelated and must not be reused for the MUX.
 *
 * Thread-safety
 * -------------
 * - MuxUart_write() must be called only from the MUX task context.
 * - MuxUart_open() and MuxUart_close() must be called before the MUX task
 *   starts and after it has stopped, respectively.
 * - The RX callback runs in UART ISR context; ring buffer writes are
 *   protected with HwiP_disable()/restore().
 */

#ifndef MUX_UART_H
#define MUX_UART_H

#include "../mux_common.h"
#include "../mux_buffer.h"

/* FreeRTOS */
#include <FreeRTOS.h>
#include <task.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * Configuration constants
 *--------------------------------------------------------------------------*/

/*!
 * Default baud rate for the MUX UART.
 * Override by passing a different value to MuxUart_open().
 * Common options: 115200, 921600, 1000000.
 */
#define MUX_UART_DEFAULT_BAUD   (115200UL)

/*!
 * Number of bytes requested per UART2_read() call (RX staging chunk size).
 * Should be >= the UART2 driver's internal DMA ring buffer size (default 32).
 * Larger values reduce callback frequency at the cost of slightly higher
 * worst-case latency between first byte arriving and ring buffer write.
 */
#define MUX_UART_RX_CHUNK       (64U)

/*---------------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------------*/

/*!
 * @brief Open the UART2 instance and start background RX.
 *
 * Configures UART2 with:
 *   - 8N1 framing, no flow control
 *   - TX: blocking mode (MuxUart_write blocks until all bytes sent)
 *   - RX: callback mode  (muxUart_rxCallback fires for each completed read)
 *
 * Issues the first UART2_read() call to arm the RX path.  Subsequent reads
 * are re-armed automatically from within the RX callback.
 *
 * @param uartIndex    UART2 instance index from SysConfig (e.g. CONFIG_MUX_UART).
 * @param baudRate     Baud rate in bits/second (e.g. MUX_UART_DEFAULT_BAUD).
 * @param rxBuf        Intermediary ring buffer owned by the MUX task.
 *                     Must remain valid for the lifetime of the UART session.
 * @param muxTask      Handle of the MUX FreeRTOS task.  The RX callback calls
 *                     xTaskNotifyFromISR() on this handle to wake the task.
 *                     Must be non-NULL and valid before calling this function.
 * @param rxNotifyBit  Notification bit to set in the MUX task's notification
 *                     value when RX bytes arrive (e.g. MUX_NOTIFY_RX_BIT).
 *
 * @return MUX_SUCCESS      UART opened and RX armed.
 * @return MUX_ERR_INVALID  NULL pointer argument.
 * @return MUX_ERR_UART     UART2_open() or initial UART2_read() failed.
 */
MuxErr_t MuxUart_open(uint8_t uartIndex, uint32_t baudRate,
                      MuxRingBuf_t *rxBuf, TaskHandle_t muxTask,
                      uint32_t rxNotifyBit);

/*!
 * @brief Close the UART2 instance and stop the RX path.
 *
 * Safe to call only when the MUX task is no longer running and no further
 * MuxUart_write() calls will be made.
 */
void MuxUart_close(void);

/*!
 * @brief Blocking write of @p len bytes to the MUX UART.
 *
 * Must be called only from the MUX task context (not from an ISR).
 * Blocks until all bytes have been transmitted or a hardware error occurs.
 *
 * @param buf  Pointer to the data to transmit (encoded HDLC frame).
 * @param len  Number of bytes to transmit.  Must be > 0.
 *
 * @return MUX_SUCCESS     All bytes transmitted.
 * @return MUX_ERR_INVALID NULL pointer or zero length.
 * @return MUX_ERR_UART    UART2_write() reported a hardware error.
 */
MuxErr_t MuxUart_write(const uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* MUX_UART_H */
