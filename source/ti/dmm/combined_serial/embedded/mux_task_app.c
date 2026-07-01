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
 * @file  mux_task_app.c
 * @brief FreeRTOS MUX task implementation for the embedded TI Combined Serial
 *        Interface.
 *
 * See mux_task_app.h for the architecture overview and thread-safety contract.
 *
 * Assert policy (same as rest of combined_serial layer):
 *   Assert before every error return that represents a programming mistake.
 *   Do NOT assert MUX_ERR_CRC (runtime line-noise event).
 *   Do NOT assert MUX_ERR_NO_PACKET (normal drain-complete condition).
 *   configASSERT() is used for FreeRTOS object creation failures — these are
 *   unrecoverable and must be caught in development.
 */

#include "mux_task_app.h"
#include "mux_uart.h"

#include "../mux_buffer.h"
#include "../hdlc_spinel.h"

#include <assert.h>
#include <string.h>   /* memset */

/* TI-Drivers */
#include <ti/drivers/dpl/HwiP.h>

/* FreeRTOS */
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

/*---------------------------------------------------------------------------
 * Module-private state  (singleton — one MUX task per device)
 *--------------------------------------------------------------------------*/

/*!
 * Task notification bits used to wake the MUX task.
 * TX_BIT: set by MuxTask_sendPacket() after enqueueing a TX message.
 * RX_BIT: set by the UART ISR (via xTaskNotifyFromISR) when new bytes arrive.
 */
typedef enum
{
    MUX_NOTIFY_TX_BIT = (1UL << 0U),   /*!< TX message enqueued */
    MUX_NOTIFY_RX_BIT = (1UL << 1U),   /*!< RX bytes in ring buffer */
} MuxNotifyBit_t;

typedef struct
{
    TaskHandle_t           taskHandle;
    QueueHandle_t          txQueue;

    /* RX intermediary ring buffer (written by UART ISR, read by MUX task) */
    MuxRingBuf_t           rxBuf;
    uint8_t                rxBufStorage[MAX_RING_BUF_SIZE];   /* 4096 B static */

    /* Scratch buffers used only inside the MUX task — module-static to keep
     * them off the task stack (which only needs room for function frames). */
    uint8_t                rxFrameBuf[MAX_FRAME_SIZE];         /* 2048 B */
    uint8_t                rxDecodedBuf[MUX_SPINEL_BUF_MAX];  /*  517 B */
    uint8_t                txEncodedBuf[MAX_FRAME_SIZE];       /* 2048 B */

    /* Per-NLI RX callbacks registered before MuxTask_create() */
    MuxStackRxCb_t         rxCbs[MUX_NLI_COUNT];
} MuxTaskState_t;

static MuxTaskState_t gMuxTask;

/* Static task storage — avoids a heap allocation for the task TCB and stack */
static StaticTask_t         gMuxTaskTcb;
static StackType_t          gMuxTaskStack[MUX_TASK_STACK_BYTES / sizeof(StackType_t)];

/* Static queue storage */
static StaticQueue_t        gTxQueueStruct;
static uint8_t              gTxQueueStorage[MUX_QUEUE_DEPTH * sizeof(MuxQueueMsg_t)];

/*---------------------------------------------------------------------------
 * Forward declarations
 *--------------------------------------------------------------------------*/

static void muxTask_fn(void *arg);
static void muxTask_handleTx(const MuxQueueMsg_t *msg);
static void muxTask_handleRx(void);
static void muxTask_sendKeepalive(void);

/*---------------------------------------------------------------------------
 * MuxTask_registerRxCb
 *--------------------------------------------------------------------------*/

void MuxTask_registerRxCb(uint8_t nli, MuxStackRxCb_t cb)
{
    assert(nli < MUX_NLI_COUNT);   /* NLI index out of range */

    if (nli >= MUX_NLI_COUNT)
    {
        return;
    }

    gMuxTask.rxCbs[nli] = cb;
}

/*---------------------------------------------------------------------------
 * MuxTask_create
 *--------------------------------------------------------------------------*/

MuxErr_t MuxTask_create(uint8_t uartIndex, uint32_t baudRate)
{
    MuxErr_t err;

    /* Zero the entire state block (clears handles, callbacks, buffers) */
    memset(&gMuxTask, 0, sizeof(gMuxTask));

    /* ------------------------------------------------------------------
     * 1. TX queue (MuxQueueMsg_t × MUX_QUEUE_DEPTH)
     * ------------------------------------------------------------------ */
    gMuxTask.txQueue = xQueueCreateStatic(
        (UBaseType_t)MUX_QUEUE_DEPTH,
        (UBaseType_t)sizeof(MuxQueueMsg_t),
        gTxQueueStorage,
        &gTxQueueStruct);

    configASSERT(gMuxTask.txQueue != NULL);

    /* ------------------------------------------------------------------
     * 2. Intermediary RX ring buffer
     * ------------------------------------------------------------------ */
    MuxBuf_init(&gMuxTask.rxBuf,
                gMuxTask.rxBufStorage,
                (uint16_t)sizeof(gMuxTask.rxBufStorage));

    /* ------------------------------------------------------------------
     * 3. Create MUX FreeRTOS task (static allocation — no heap).
     *
     *    The task is created before MuxUart_open() so that its handle can
     *    be passed to the UART layer for xTaskNotifyFromISR() delivery.
     *    This is safe because MuxTask_create() must be called before
     *    vTaskStartScheduler() — the task will not execute until the
     *    scheduler starts, by which time the UART is already open.
     * ------------------------------------------------------------------ */
    gMuxTask.taskHandle = xTaskCreateStatic(
        muxTask_fn,
        "MuxTask",
        (uint32_t)(MUX_TASK_STACK_BYTES / sizeof(StackType_t)),
        NULL,
        (UBaseType_t)MUX_TASK_PRIORITY,
        gMuxTaskStack,
        &gMuxTaskTcb);

    configASSERT(gMuxTask.taskHandle != NULL);

    /* ------------------------------------------------------------------
     * 4. Open UART (arms first UART2_read internally).
     *    Pass the task handle and RX notification bit so the UART ISR
     *    can wake the MUX task directly via xTaskNotifyFromISR().
     * ------------------------------------------------------------------ */
    err = MuxUart_open(uartIndex, baudRate,
                       &gMuxTask.rxBuf,
                       gMuxTask.taskHandle,
                       (uint32_t)MUX_NOTIFY_RX_BIT);

    if (err != MUX_SUCCESS)
    {
        /* UART open failure is a runtime error — return it to the caller */
        return err;
    }

    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * MuxTask_sendPacket
 *--------------------------------------------------------------------------*/

MuxErr_t MuxTask_sendPacket(uint8_t nli, const uint8_t *buf, uint16_t len)
{
    MuxQueueMsg_t msg;
    BaseType_t    result;

    assert(buf != NULL);                /* NULL payload pointer       */
    assert(len != 0U);                  /* zero-length packet is a bug */
    assert(len <= MUX_MSG_BUF_LEN);    /* payload exceeds queue message size */
    assert(nli < MUX_NLI_COUNT);       /* NLI out of range           */

    if (!buf || len == 0U || len > MUX_MSG_BUF_LEN || nli >= MUX_NLI_COUNT)
    {
        return MUX_ERR_INVALID;
    }

    msg.nli = nli;
    msg.len = len;
    memcpy(msg.buf, buf, len);

    /*
     * Non-blocking enqueue.  The payload has been copied so the caller's
     * buffer is safe to reuse immediately.
     */
    result = xQueueSend(gMuxTask.txQueue, &msg, 0);

    if (result != pdPASS)
    {
        assert(false);   /* TX queue full — host or UART not keeping up */
        return MUX_ERR_QUEUE;
    }

    /*
     * Wake the MUX task.  xTaskNotify with eSetBits is idempotent — if the
     * task is already awake (processing a previous message), the bit stays
     * set and the task will drain the full queue on its next iteration.
     */
    (void)xTaskNotify(gMuxTask.taskHandle, (uint32_t)MUX_NOTIFY_TX_BIT, eSetBits);

    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * muxTask_sendKeepalive  (private — called from MUX task only)
 *--------------------------------------------------------------------------*/

static void muxTask_sendKeepalive(void)
{
    uint16_t encodedLen = 0U;
    MuxErr_t err;

    /*
     * Keepalive frame has no application payload (payload = NULL, len = 0).
     * MuxSpinelHdlc_encode handles the zero-length payload case.
     */
    err = MuxSpinelHdlc_encode(
            (uint8_t)MUX_NLI_KEEPALIVE,
            (uint32_t)CMD_KEEPALIVE,
            NULL,          /* no payload */
            0U,
            gMuxTask.txEncodedBuf,
            (uint16_t)sizeof(gMuxTask.txEncodedBuf),
            &encodedLen);

    assert(err == MUX_SUCCESS);   /* internal sizing error */

    if (err != MUX_SUCCESS)
    {
        return;
    }

    err = MuxUart_write(gMuxTask.txEncodedBuf, encodedLen);

    assert(err == MUX_SUCCESS);   /* UART TX error during keepalive */
    (void)err;
}

/*---------------------------------------------------------------------------
 * muxTask_handleTx  (private — called from MUX task only)
 *--------------------------------------------------------------------------*/

static void muxTask_handleTx(const MuxQueueMsg_t *msg)
{
    uint16_t encodedLen = 0U;
    MuxErr_t err;

    assert(msg != NULL);   /* NULL message pointer — internal logic error */

    if (!msg)
    {
        return;
    }

    err = MuxSpinelHdlc_encode(
            msg->nli,
            (uint32_t)SPINEL_CMD_PROP_VALUE_IS,
            msg->buf,
            msg->len,
            gMuxTask.txEncodedBuf,
            (uint16_t)sizeof(gMuxTask.txEncodedBuf),
            &encodedLen);

    assert(err == MUX_SUCCESS);   /* encoding error — buffer sizing or invalid NLI */

    if (err != MUX_SUCCESS)
    {
        return;
    }

    err = MuxUart_write(gMuxTask.txEncodedBuf, encodedLen);

    assert(err == MUX_SUCCESS);   /* UART TX error */
    (void)err;
}

/*---------------------------------------------------------------------------
 * muxTask_handleRx  (private — called from MUX task only)
 *
 * Drains all complete HDLC frames from the ring buffer, decodes each one,
 * and dispatches the payload to the appropriate per-NLI callback.
 *--------------------------------------------------------------------------*/

static void muxTask_handleRx(void)
{
    MuxErr_t         err;
    uintptr_t        key;
    uint16_t         frameLen;
    uint16_t         decodedLen;
    uint8_t          nli;
    uint32_t         cmd;
    const uint8_t   *payloadPtr;
    uint16_t         payloadLen;

    /*
     * Drain all frames that are currently in the ring buffer.
     * Each iteration extracts one complete HDLC frame (delimited by 0x7E).
     * Loop exits when MuxBuf_extractFrame returns MUX_ERR_NO_PACKET
     * (no complete frame available yet).
     */
    while (1)
    {
        /*
         * Critical section: prevent the UART ISR from writing to the ring
         * buffer concurrently while the MUX task is reading from it.
         */
        key      = HwiP_disable();
        frameLen = (uint16_t)sizeof(gMuxTask.rxFrameBuf);
        err      = MuxBuf_extractFrame(&gMuxTask.rxBuf,
                                       gMuxTask.rxFrameBuf,
                                       frameLen,
                                       &frameLen);
        HwiP_restore(key);

        if (err == MUX_ERR_NO_PACKET)
        {
            /* Normal: ring buffer drained, no more complete frames */
            break;
        }

        if (err != MUX_SUCCESS)
        {
            /*
             * Unexpected ring buffer error (overflow, etc.).
             * The frame has been discarded by extractFrame.  Continue
             * draining in case more frames follow.
             * Not asserted here — runtime robustness for corrupted data.
             */
            continue;
        }

        /* ------------------------------------------------------------------
         * HDLC decode: unescape and verify CRC
         * ------------------------------------------------------------------ */
        decodedLen = (uint16_t)sizeof(gMuxTask.rxDecodedBuf);
        err        = MuxHdlc_decode(gMuxTask.rxFrameBuf, frameLen,
                                    gMuxTask.rxDecodedBuf, decodedLen,
                                    &decodedLen);

        if (err == MUX_ERR_CRC)
        {
            /* CRC mismatch is a recoverable runtime event (line noise).
             * Not asserted — discard and continue. */
            continue;
        }

        if (err != MUX_SUCCESS)
        {
            /* Other decode errors (framing, overflow).  Not asserted — could
             * be caused by partial frames at startup or link glitches. */
            continue;
        }

        /* ------------------------------------------------------------------
         * Spinel parse: extract NLI, CMD, and payload pointer
         * ------------------------------------------------------------------ */
        err = MuxSpinel_parseFrame(gMuxTask.rxDecodedBuf, decodedLen,
                                   &nli, &cmd,
                                   &payloadPtr, &payloadLen);

        if (err != MUX_SUCCESS)
        {
            /* Malformed Spinel frame — discard */
            continue;
        }

        /* ------------------------------------------------------------------
         * Dispatch by NLI
         * ------------------------------------------------------------------ */
        if (nli == (uint8_t)MUX_NLI_KEEPALIVE)
        {
            if (cmd == (uint32_t)CMD_KEEPALIVE_ACK)
            {
                /*
                 * Host acknowledged our keepalive.
                 * Dead-host detection and recovery are left for a future phase.
                 * For now, just acknowledge receipt silently.
                 */
            }
            /* Any other keepalive-channel CMD is ignored */
        }
        else if (nli < (uint8_t)MUX_NLI_COUNT)
        {
            if (gMuxTask.rxCbs[nli] != NULL)
            {
                gMuxTask.rxCbs[nli](payloadPtr, payloadLen);
            }
            /* If no callback is registered, the packet is silently dropped.
             * Not asserted — stacks may not register callbacks during init. */
        }
        /* NLI values >= MUX_NLI_COUNT are silently ignored */
    }
}

/*---------------------------------------------------------------------------
 * muxTask_fn  (MUX task entry point)
 *--------------------------------------------------------------------------*/

static void muxTask_fn(void *arg)
{
    BaseType_t  notified;
    uint32_t    notifiedBits;

    (void)arg;

    /* Send an initial keepalive so the host knows the device is up */
    muxTask_sendKeepalive();

    for (;;)
    {
        /*
         * Block until either:
         *   a) MUX_NOTIFY_TX_BIT set by MuxTask_sendPacket()  (stack → MUX → host)
         *   b) MUX_NOTIFY_RX_BIT set by UART ISR              (host → MUX → stack)
         *   c) Timeout expires                                 (send periodic keepalive)
         *
         * ulBitsToClearOnEntry = 0: preserve any bits that arrived while we
         *   were processing the previous iteration.
         * ulBitsToClearOnExit  = all: atomically snapshot and clear so no
         *   notification is lost between reading and re-entering the wait.
         */
        notifiedBits = 0U;
        notified = xTaskNotifyWait(0U,
                                   (uint32_t)(MUX_NOTIFY_TX_BIT | MUX_NOTIFY_RX_BIT),
                                   &notifiedBits,
                                   pdMS_TO_TICKS(MUX_KEEPALIVE_PERIOD_MS));

        if (notifiedBits & (uint32_t)MUX_NOTIFY_TX_BIT)
        {
            MuxQueueMsg_t txMsg;

            /*
             * Drain the entire TX queue.  Multiple sendPacket() calls may have
             * been coalesced into a single TX_BIT notification while the task
             * was busy — process them all before sleeping again.
             */
            while (xQueueReceive(gMuxTask.txQueue, &txMsg, 0) == pdPASS)
            {
                muxTask_handleTx(&txMsg);
            }
        }

        if (notifiedBits & (uint32_t)MUX_NOTIFY_RX_BIT)
        {
            /*
             * UART ISR wrote bytes to the ring buffer and set RX_BIT.
             * Drain all complete HDLC frames now available.
             */
            muxTask_handleRx();
        }

        if (notified == pdFALSE)
        {
            /*
             * Timeout — no TX or RX activity for MUX_KEEPALIVE_PERIOD_MS.
             * Send the periodic keepalive to the host.
             */
            muxTask_sendKeepalive();
        }
    }
}
