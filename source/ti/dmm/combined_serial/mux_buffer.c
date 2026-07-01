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
 * @file  mux_buffer.c
 * @brief Byte-oriented ring buffer implementation for the TI Combined Serial
 *        MUX layer.
 *
 * Multi-byte reads and writes use memcpy in up to two passes to handle the
 * wrap-around case efficiently.  Single-byte helpers avoid the memcpy
 * overhead for hot paths such as per-byte UART RX callbacks.
 *
 * Assert policy:
 *   Every error path is guarded by an assert() that fires before the error
 *   return so that bugs are caught immediately during development.  The
 *   runtime error-return that follows remains active in release builds
 *   (NDEBUG defined).  Remove asserts when the code is proven stable.
 */

#include "mux_buffer.h"

#include <assert.h>   /* assert() — remove or define NDEBUG when stable */
#include <string.h>   /* memcpy  */

/*---------------------------------------------------------------------------
 * Lifecycle
 *--------------------------------------------------------------------------*/

void MuxBuf_init(MuxRingBuf_t *rb, uint8_t *storage, uint16_t capacity)
{
    if (!rb || !storage || capacity == 0U)
    {
        return;
    }
    rb->buf   = storage;
    rb->cap   = capacity;
    rb->head  = 0U;
    rb->tail  = 0U;
    rb->count = 0U;
}

void MuxBuf_reset(MuxRingBuf_t *rb)
{
    if (!rb)
    {
        return;
    }
    rb->head  = 0U;
    rb->tail  = 0U;
    rb->count = 0U;
}

/*---------------------------------------------------------------------------
 * Status queries
 * (no assert — these tolerate NULL gracefully by returning 0/false)
 *--------------------------------------------------------------------------*/

uint16_t MuxBuf_available(const MuxRingBuf_t *rb)
{
    return rb ? rb->count : 0U;
}

uint16_t MuxBuf_free(const MuxRingBuf_t *rb)
{
    return rb ? (uint16_t)(rb->cap - rb->count) : 0U;
}

bool MuxBuf_isEmpty(const MuxRingBuf_t *rb)
{
    return (!rb || rb->count == 0U);
}

bool MuxBuf_isFull(const MuxRingBuf_t *rb)
{
    return (rb != NULL) && (rb->count == rb->cap);
}

/*---------------------------------------------------------------------------
 * Write
 *--------------------------------------------------------------------------*/

MuxErr_t MuxBuf_write(MuxRingBuf_t *rb, const uint8_t *src, uint16_t len)
{
    uint16_t firstChunk;

    if (!rb || !src || len == 0U)
    {
        return MUX_ERR_INVALID;
    }

    if (len > (uint16_t)(rb->cap - rb->count))
    {
        return MUX_ERR_FULL;
    }

    if ((uint16_t)(rb->tail + len) <= rb->cap)
    {
        /* Contiguous free region — single copy */
        memcpy(&rb->buf[rb->tail], src, len);
    }
    else
    {
        /* Wrap-around: split into two contiguous copies */
        firstChunk = (uint16_t)(rb->cap - rb->tail);
        memcpy(&rb->buf[rb->tail], src,             firstChunk);
        memcpy( rb->buf,           src + firstChunk, len - firstChunk);
    }

    rb->tail  = (uint16_t)((rb->tail + len) % rb->cap);
    rb->count = (uint16_t)(rb->count + len);
    return MUX_SUCCESS;
}

MuxErr_t MuxBuf_writeByte(MuxRingBuf_t *rb, uint8_t byte)
{
    if (!rb)
    {
        return MUX_ERR_INVALID;
    }

    if (rb->count == rb->cap)
    {
        return MUX_ERR_FULL;
    }

    rb->buf[rb->tail] = byte;
    rb->tail          = (uint16_t)((rb->tail + 1U) % rb->cap);
    rb->count++;
    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Read
 *--------------------------------------------------------------------------*/

MuxErr_t MuxBuf_read(MuxRingBuf_t *rb, uint8_t *dst, uint16_t len)
{
    uint16_t firstChunk;

    if (!rb || !dst || len == 0U)
    {
        return MUX_ERR_INVALID;
    }

    if (len > rb->count)
    {
        return MUX_ERR_NO_SPACE;
    }

    if ((uint16_t)(rb->head + len) <= rb->cap)
    {
        memcpy(dst, &rb->buf[rb->head], len);
    }
    else
    {
        firstChunk = (uint16_t)(rb->cap - rb->head);
        memcpy(dst,             &rb->buf[rb->head], firstChunk);
        memcpy(dst + firstChunk, rb->buf,           len - firstChunk);
    }

    rb->head  = (uint16_t)((rb->head + len) % rb->cap);
    rb->count = (uint16_t)(rb->count - len);
    return MUX_SUCCESS;
}

MuxErr_t MuxBuf_readByte(MuxRingBuf_t *rb, uint8_t *out)
{
    if (!rb || !out)
    {
        return MUX_ERR_INVALID;
    }

    if (rb->count == 0U)
    {
        return MUX_ERR_EMPTY;
    }

    *out     = rb->buf[rb->head];
    rb->head = (uint16_t)((rb->head + 1U) % rb->cap);
    rb->count--;
    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Peek
 *--------------------------------------------------------------------------*/

MuxErr_t MuxBuf_peek(const MuxRingBuf_t *rb, uint16_t offset,
                     uint8_t *dst, uint16_t len)
{
    uint16_t startIdx;
    uint16_t firstChunk;

    if (!rb || !dst || len == 0U)
    {
        return MUX_ERR_INVALID;
    }

    /* Guard against uint16_t overflow in the comparison */
    if ((uint32_t)offset + (uint32_t)len > (uint32_t)rb->count)
    {
        return MUX_ERR_NO_SPACE;
    }

    startIdx = (uint16_t)(((uint32_t)rb->head + offset) % rb->cap);

    if ((uint16_t)(startIdx + len) <= rb->cap)
    {
        memcpy(dst, &rb->buf[startIdx], len);
    }
    else
    {
        firstChunk = (uint16_t)(rb->cap - startIdx);
        memcpy(dst,             &rb->buf[startIdx], firstChunk);
        memcpy(dst + firstChunk, rb->buf,           len - firstChunk);
    }

    return MUX_SUCCESS;
}

MuxErr_t MuxBuf_peekByte(const MuxRingBuf_t *rb, uint16_t offset,
                         uint8_t *out)
{
    if (!rb || !out)
    {
        return MUX_ERR_INVALID;
    }

    if (offset >= rb->count)
    {
        return MUX_ERR_NO_SPACE;
    }

    *out = rb->buf[((uint32_t)rb->head + offset) % rb->cap];
    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Discard
 *--------------------------------------------------------------------------*/

MuxErr_t MuxBuf_discard(MuxRingBuf_t *rb, uint16_t len)
{
    if (!rb)
    {
        return MUX_ERR_INVALID;
    }

    if (len > rb->count)
    {
        return MUX_ERR_NO_SPACE;
    }

    rb->head  = (uint16_t)(((uint32_t)rb->head + len) % rb->cap);
    rb->count = (uint16_t)(rb->count - len);
    return MUX_SUCCESS;
}

/*---------------------------------------------------------------------------
 * HDLC frame extraction
 *--------------------------------------------------------------------------*/

/*!
 * Scanning strategy:
 *
 *  Pass 1 — find opening flag:
 *    Walk forward from the current head until a 0x7E byte is found.
 *    Any bytes before it are garbage (framing error); discard them.
 *    If no 0x7E is present at all, reset the buffer and return NO_PACKET.
 *
 *  Pass 2 — skip inter-frame fill:
 *    After the opening 0x7E, any further consecutive 0x7E bytes are
 *    inter-frame fill.  Keep exactly one 0x7E as the opening flag and
 *    discard the rest.
 *
 *  Pass 3 — find closing flag:
 *    Scan from the first non-flag byte onwards.  The first 0x7E encountered
 *    after at least one non-flag payload byte is the closing flag.
 *
 *  On success: peek the entire frame (rb[0..frameEnd]) into dst, discard it.
 */
MuxErr_t MuxBuf_extractFrame(MuxRingBuf_t *rb, uint8_t *dst,
                             uint16_t maxLen, uint16_t *frameLen)
{
    uint16_t avail;
    uint16_t offset;
    uint8_t  b;
    bool     payloadSeen;
    uint16_t frameEnd;

    if (!rb || !dst || !frameLen || maxLen == 0U)
    {
        return MUX_ERR_INVALID;
    }

    /* ------------------------------------------------------------------
     * Pass 1: Locate the opening 0x7E flag.
     * ------------------------------------------------------------------ */
    avail = MuxBuf_available(rb);

    for (offset = 0U; offset < avail; offset++)
    {
        MuxBuf_peekByte(rb, offset, &b);
        if (b == HDLC_FLAG)
        {
            break;
        }
    }

    if (offset == avail)
    {
        /* No flag byte found — discard all garbage and wait for more data.
         * Not asserted: legitimate at start-up or after a line glitch. */
        MuxBuf_reset(rb);
        return MUX_ERR_NO_PACKET;
    }

    if (offset > 0U)
    {
        /* Discard garbage bytes preceding the first flag */
        MuxBuf_discard(rb, offset);
        avail = (uint16_t)(avail - offset);
    }

    /* rb[0] == HDLC_FLAG now */

    /* ------------------------------------------------------------------
     * Pass 2: Skip consecutive 0x7E (inter-frame fill).
     *         Keep one 0x7E as the opening flag.
     * ------------------------------------------------------------------ */
    for (offset = 1U; offset < avail; offset++)
    {
        MuxBuf_peekByte(rb, offset, &b);
        if (b != HDLC_FLAG)
        {
            break;
        }
    }

    /* (offset - 1) extra leading flags to discard */
    if (offset > 1U)
    {
        MuxBuf_discard(rb, (uint16_t)(offset - 1U));
        avail = (uint16_t)(avail - (offset - 1U));
    }

    /* Need at least: opening 0x7E + 1 payload byte + closing 0x7E.
     * Not asserted: legitimate "more bytes needed" condition. */
    if (avail < 3U)
    {
        return MUX_ERR_NO_PACKET;
    }

    /* ------------------------------------------------------------------
     * Pass 3: Scan for closing 0x7E after payload content.
     * ------------------------------------------------------------------ */
    payloadSeen = false;
    frameEnd    = 0U;

    for (offset = 1U; offset < avail; offset++)
    {
        MuxBuf_peekByte(rb, offset, &b);

        if (b == HDLC_FLAG)
        {
            if (payloadSeen)
            {
                frameEnd = offset;   /* closing flag found */
                break;
            }
            /* Another 0x7E before any payload — keep scanning */
        }
        else
        {
            payloadSeen = true;
        }
    }

    if (frameEnd == 0U)
    {
        /* Incomplete frame — not asserted, normal "wait" condition */
        return MUX_ERR_NO_PACKET;
    }

    /* ------------------------------------------------------------------
     * Frame is rb[0 .. frameEnd] inclusive, length = frameEnd + 1.
     * ------------------------------------------------------------------ */
    {
        uint16_t len = (uint16_t)(frameEnd + 1U);

        if (len > maxLen)
        {
            /* Oversized frame — discard it rather than stalling the consumer */
            MuxBuf_discard(rb, len);
            return MUX_ERR_OVERFLOW;
        }

        MuxBuf_peek(rb, 0U, dst, len);
        MuxBuf_discard(rb, len);

        *frameLen = len;
        return MUX_SUCCESS;
    }
}
