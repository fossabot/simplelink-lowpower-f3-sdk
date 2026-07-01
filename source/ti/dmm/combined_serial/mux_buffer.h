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
 * @file  mux_buffer.h
 * @brief Byte-oriented ring buffer for the TI Combined Serial MUX layer.
 *
 * The ring buffer accumulates raw UART/SPI RX bytes until a complete HDLC
 * frame (0x7E ... 0x7E) is available for extraction by the MUX task.
 *
 * Design notes:
 *  - Caller provides the backing storage; no dynamic allocation is performed.
 *  - Internal state is tracked with a byte count so that full and empty are
 *    always unambiguous without wasting a slot.
 *  - Multi-byte reads and writes use memcpy to handle wrap-around in two
 *    passes; single-byte helpers are provided for hot paths.
 *
 * @note This implementation is NOT internally thread-safe.
 *       When producer (UART RX callback) and consumer (MUX task) run in
 *       different execution contexts the caller must provide external
 *       synchronisation (e.g. a FreeRTOS critical section, mutex, or ISR
 *       disable/enable pair around every call).
 */

#ifndef MUX_BUFFER_H
#define MUX_BUFFER_H

#include "mux_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * Ring-buffer descriptor
 *--------------------------------------------------------------------------*/

/*!
 * @brief Ring-buffer control block.
 *
 * Initialise with MuxBuf_init().  Do not manipulate fields directly.
 */
typedef struct
{
    uint8_t  *buf;    /*!< Pointer to caller-provided backing storage        */
    uint16_t  cap;    /*!< Capacity of backing storage in bytes              */
    uint16_t  head;   /*!< Index of the next byte to read                    */
    uint16_t  tail;   /*!< Index of the next free slot to write into         */
    uint16_t  count;  /*!< Number of valid bytes currently stored            */
} MuxRingBuf_t;

/*---------------------------------------------------------------------------
 * Lifecycle
 *--------------------------------------------------------------------------*/

/*!
 * @brief Initialise a ring-buffer descriptor.
 *
 * @param rb        Descriptor to initialise (must not be NULL).
 * @param storage   Caller-allocated backing array of at least @p capacity bytes.
 * @param capacity  Size of @p storage in bytes.  Must be > 0.
 */
void MuxBuf_init(MuxRingBuf_t *rb, uint8_t *storage, uint16_t capacity);

/*!
 * @brief Discard all content and reset indices to zero.
 *
 * Safe to call from any context as long as no concurrent access occurs.
 */
void MuxBuf_reset(MuxRingBuf_t *rb);

/*---------------------------------------------------------------------------
 * Status queries
 *--------------------------------------------------------------------------*/

/*! @return Bytes available to read without blocking. */
uint16_t MuxBuf_available(const MuxRingBuf_t *rb);

/*! @return Free bytes that can be written before the buffer is full. */
uint16_t MuxBuf_free(const MuxRingBuf_t *rb);

/*! @return true if the buffer contains no data. */
bool MuxBuf_isEmpty(const MuxRingBuf_t *rb);

/*! @return true if the buffer has no free space. */
bool MuxBuf_isFull(const MuxRingBuf_t *rb);

/*---------------------------------------------------------------------------
 * Write — producer side
 *--------------------------------------------------------------------------*/

/*!
 * @brief Append @p len bytes from @p src.
 *
 * The write is all-or-nothing: either all @p len bytes are stored or none are.
 *
 * @return MUX_SUCCESS      All bytes written.
 * @return MUX_ERR_FULL     Insufficient free space; buffer unchanged.
 * @return MUX_ERR_INVALID  @p rb or @p src is NULL, or @p len is 0.
 */
MuxErr_t MuxBuf_write(MuxRingBuf_t *rb, const uint8_t *src, uint16_t len);

/*!
 * @brief Append a single byte.
 *
 * @return MUX_SUCCESS or MUX_ERR_FULL.
 */
MuxErr_t MuxBuf_writeByte(MuxRingBuf_t *rb, uint8_t byte);

/*---------------------------------------------------------------------------
 * Read / peek / discard — consumer side
 *--------------------------------------------------------------------------*/

/*!
 * @brief Copy and consume @p len bytes into @p dst.
 *
 * @return MUX_SUCCESS       All bytes read and consumed.
 * @return MUX_ERR_NO_SPACE  Fewer than @p len bytes available; buffer unchanged.
 * @return MUX_ERR_INVALID   @p rb or @p dst is NULL, or @p len is 0.
 */
MuxErr_t MuxBuf_read(MuxRingBuf_t *rb, uint8_t *dst, uint16_t len);

/*!
 * @brief Copy and consume a single byte.
 *
 * @return MUX_SUCCESS or MUX_ERR_EMPTY.
 */
MuxErr_t MuxBuf_readByte(MuxRingBuf_t *rb, uint8_t *out);

/*!
 * @brief Non-consuming peek: copy @p len bytes at @p offset from the read head.
 *
 * The read head is not advanced.
 *
 * @param rb      Ring buffer to inspect.
 * @param offset  Distance (in bytes) from the current read head (0 = next byte).
 * @param dst     Destination buffer for the copied bytes.
 * @param len     Number of bytes to copy.
 *
 * @return MUX_SUCCESS       Data copied.
 * @return MUX_ERR_NO_SPACE  (@p offset + @p len) exceeds available bytes.
 * @return MUX_ERR_INVALID   NULL pointer or @p len is 0.
 */
MuxErr_t MuxBuf_peek(const MuxRingBuf_t *rb, uint16_t offset,
                     uint8_t *dst, uint16_t len);

/*!
 * @brief Non-consuming peek of a single byte at @p offset from the read head.
 *
 * @return MUX_SUCCESS or MUX_ERR_NO_SPACE.
 */
MuxErr_t MuxBuf_peekByte(const MuxRingBuf_t *rb, uint16_t offset,
                         uint8_t *out);

/*!
 * @brief Advance the read head by @p len bytes (consume without copying).
 *
 * @return MUX_SUCCESS or MUX_ERR_NO_SPACE.
 */
MuxErr_t MuxBuf_discard(MuxRingBuf_t *rb, uint16_t len);

/*---------------------------------------------------------------------------
 * HDLC frame extraction
 *--------------------------------------------------------------------------*/

/*!
 * @brief Find and extract the next complete raw HDLC frame from the buffer.
 *
 * An HDLC frame is defined as:
 *   [0x7E] [one or more non-flag bytes] [0x7E]
 *
 * Behaviour:
 *  - Any leading consecutive 0x7E bytes (inter-frame fill) are discarded
 *    before searching, keeping one 0x7E as the opening flag.
 *  - The first 0x7E encountered after at least one non-flag payload byte is
 *    treated as the closing flag.
 *  - On success the raw frame — including both flag bytes — is copied into
 *    @p dst and consumed from the ring buffer.  Note that the payload still
 *    contains HDLC escape sequences and the CRC; call MuxHdlc_decode() to
 *    strip them.
 *  - If no 0x7E is present at all the buffer is reset (framing error recovery)
 *    and MUX_ERR_NO_PACKET is returned.
 *
 * @param[in]  rb        Ring buffer to search.
 * @param[out] dst       Destination for the raw frame bytes.
 * @param[in]  maxLen    Capacity of @p dst in bytes.
 * @param[out] frameLen  Set to the number of bytes written to @p dst on success.
 *
 * @return MUX_SUCCESS        A complete frame was found and returned.
 * @return MUX_ERR_NO_PACKET  No complete frame available yet; caller should wait
 *                            for more RX bytes.
 * @return MUX_ERR_OVERFLOW   Frame is complete but exceeds @p maxLen; the frame
 *                            is discarded from the ring buffer.
 * @return MUX_ERR_INVALID    NULL pointer passed.
 */
MuxErr_t MuxBuf_extractFrame(MuxRingBuf_t *rb, uint8_t *dst,
                             uint16_t maxLen, uint16_t *frameLen);

#ifdef __cplusplus
}
#endif

#endif /* MUX_BUFFER_H */
