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
 * @file  mux_uart.c
 * @brief UART2 driver integration for the embedded TI Combined Serial MUX.
 *
 * Assert policy (same as rest of combined_serial layer):
 *   Programming errors (NULL pointers, bad state) are asserted before the
 *   runtime error return.  Hardware-level errors (UART2 open failure, DMA
 *   overrun) are NOT asserted — they are runtime events returned as
 *   MUX_ERR_UART so the caller can handle or log them.
 */

#include "mux_uart.h"

#include <assert.h>

/* TI-Drivers */
#include <ti/drivers/UART2.h>
#include <ti/drivers/dpl/HwiP.h>   /* HwiP_disable / HwiP_restore */

/* FreeRTOS */
#include <FreeRTOS.h>
#include <task.h>

/*---------------------------------------------------------------------------
 * Module-private state  (singleton — one MUX UART per device)
 *--------------------------------------------------------------------------*/

typedef struct
{
    UART2_Handle      handle;                        /*!< Open UART2 handle          */
    MuxRingBuf_t     *rxBuf;                         /*!< Intermediary ring buffer   */
    TaskHandle_t      muxTask;                       /*!< MUX task to notify on RX   */
    uint32_t          rxNotifyBit;                   /*!< Notification bit for RX    */
    uint8_t           rxStagingBuf[MUX_UART_RX_CHUNK]; /*!< DMA→ringbuf staging     */
    bool              isOpen;                        /*!< Guard against double-open  */
} MuxUartState_t;

static MuxUartState_t gUart = { 0 };

/*---------------------------------------------------------------------------
 * Forward declaration
 *--------------------------------------------------------------------------*/

static void muxUart_rxCallback(UART2_Handle handle, void *buf, size_t count,
                                void *userArg, int_fast16_t status);

/*---------------------------------------------------------------------------
 * MuxUart_open
 *--------------------------------------------------------------------------*/

MuxErr_t MuxUart_open(uint8_t uartIndex, uint32_t baudRate,
                      MuxRingBuf_t *rxBuf, TaskHandle_t muxTask,
                      uint32_t rxNotifyBit)
{
    UART2_Params params;
    int_fast16_t status;

    assert(rxBuf    != NULL);   /* NULL ring buffer pointer  */
    assert(muxTask  != NULL);   /* NULL task handle          */
    assert(!gUart.isOpen);      /* MuxUart_open called twice */

    if (!rxBuf || !muxTask)
    {
        return MUX_ERR_INVALID;
    }

    /* Store references before opening so the callback can use them
     * immediately after UART2_open() arms the first read. */
    gUart.rxBuf       = rxBuf;
    gUart.muxTask     = muxTask;
    gUart.rxNotifyBit = rxNotifyBit;

    /* Configure UART2 parameters */
    UART2_Params_init(&params);
    params.baudRate       = baudRate;
    params.dataLength     = UART2_DataLen_8;
    params.stopBits       = UART2_StopBits_1;
    params.parityType     = UART2_Parity_NONE;

    /* TX: blocking — MuxUart_write() blocks until all bytes are sent */
    params.writeMode      = UART2_Mode_BLOCKING;

    /* RX: callback — muxUart_rxCallback() fires when bytes arrive */
    params.readMode           = UART2_Mode_CALLBACK;
    params.readCallback       = muxUart_rxCallback;
    params.readReturnMode     = UART2_ReadReturnMode_PARTIAL;

    gUart.handle = UART2_open(uartIndex, &params);

    if (gUart.handle == NULL)
    {
        /* UART2_open() failed — hardware not available or index invalid */
        gUart.rxBuf       = NULL;
        gUart.muxTask     = NULL;
        gUart.rxNotifyBit = 0U;
        return MUX_ERR_UART;
    }

    /* Arm the first read — subsequent reads are re-armed from the callback */
    status = UART2_read(gUart.handle,
                        gUart.rxStagingBuf,
                        MUX_UART_RX_CHUNK,
                        NULL);

    if (status != UART2_STATUS_SUCCESS)
    {
        UART2_close(gUart.handle);
        gUart.handle      = NULL;
        gUart.rxBuf       = NULL;
        gUart.muxTask     = NULL;
        gUart.rxNotifyBit = 0U;
        return MUX_ERR_UART;
    }

    gUart.isOpen = true;
    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * MuxUart_close
 *--------------------------------------------------------------------------*/

void MuxUart_close(void)
{
    assert(gUart.isOpen);   /* close called without a matching open */

    if (gUart.handle != NULL)
    {
        UART2_close(gUart.handle);
        gUart.handle = NULL;
    }

    gUart.rxBuf       = NULL;
    gUart.muxTask     = NULL;
    gUart.rxNotifyBit = 0U;
    gUart.isOpen      = false;
}

/*---------------------------------------------------------------------------
 * MuxUart_write
 *--------------------------------------------------------------------------*/

MuxErr_t MuxUart_write(const uint8_t *buf, uint16_t len)
{
    size_t       bytesWritten = 0U;
    int_fast16_t status;

    assert(buf  != NULL);     /* NULL buffer pointer      */
    assert(len  != 0U);       /* zero-length write is bug */
    assert(gUart.isOpen);     /* write before open        */

    if (!buf || len == 0U)
    {
        return MUX_ERR_INVALID;
    }

    status = UART2_write(gUart.handle, buf, (size_t)len, &bytesWritten);

    assert(status == UART2_STATUS_SUCCESS);   /* UART TX hardware error */

    if (status != UART2_STATUS_SUCCESS)
    {
        return MUX_ERR_UART;
    }

    assert(bytesWritten == (size_t)len);   /* partial write — should not happen in blocking mode */

    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * muxUart_rxCallback  (ISR context)
 *
 * Called by the UART2 driver after each completed UART2_read().
 * Execution context: UART hardware interrupt (ISR).
 *
 * This function must:
 *   1. Copy received bytes into the intermediary ring buffer (critical section).
 *   2. Set MUX_NOTIFY_RX_BIT in the MUX task's notification value.
 *   3. Re-arm the next UART2_read() so bytes keep flowing.
 *--------------------------------------------------------------------------*/

static void muxUart_rxCallback(UART2_Handle handle, void *buf, size_t count,
                                void *userArg, int_fast16_t status)
{
    uintptr_t         key;
    BaseType_t        higherPriorityTaskWoken = pdFALSE;
    MuxErr_t          err;

    (void)userArg;   /* unused — module state accessed via gUart */

    if (status != UART2_STATUS_SUCCESS || count == 0U)
    {
        /*
         * Hardware error (framing, overrun, break) or zero-byte callback.
         * Re-arm the read and return — the MUX task does not need to wake
         * since there is no new data to process.
         * Not asserted: hardware errors are runtime events on a real link.
         */
        UART2_read(handle, gUart.rxStagingBuf, MUX_UART_RX_CHUNK, NULL);
        return;
    }

    /*
     * Write received bytes into the intermediary ring buffer.
     * Critical section: prevents the MUX task from reading the ring buffer
     * concurrently while we are modifying it from ISR context.
     */
    key = HwiP_disable();
    err = MuxBuf_write(gUart.rxBuf, (const uint8_t *)buf, (uint16_t)count);
    HwiP_restore(key);

    /*
     * If the ring buffer was full we dropped bytes — assert to catch this
     * during development (means MUX task is not draining fast enough).
     */
    assert(err == MUX_SUCCESS);   /* ring buffer full — increase MAX_RING_BUF_SIZE or MUX task priority */

    /*
     * Wake the MUX task.  xTaskNotifyFromISR with eSetBits is safe from ISR
     * context and idempotent — if the task is already running, the bit stays
     * set so it will call muxTask_handleRx() again on its next iteration.
     * The scheduler yields to the MUX task after this ISR exits if it has
     * higher priority than the interrupted task.
     */
    (void)xTaskNotifyFromISR(gUart.muxTask,
                             gUart.rxNotifyBit,
                             eSetBits,
                             &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);

    /*
     * Re-arm the next read.  UART2_read() in callback mode is safe to call
     * from ISR context.  The staging buffer is not reused until this
     * call completes (next callback invocation), so reuse is safe here.
     */
    UART2_read(handle, gUart.rxStagingBuf, MUX_UART_RX_CHUNK, NULL);
}
