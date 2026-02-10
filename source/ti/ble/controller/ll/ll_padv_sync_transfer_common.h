/******************************************************************************
 @file  ll_padv_sync_transfer_common.h

 @brief This file dfines the periodic advertising sync transfer feature,
        enabling BLE devices to share synchronization data for improved
        efficiency. The module is activated by defining USE_PAST_SENDER.

 Group: WCS, BTS
 Target Device: cc23xx

 ******************************************************************************
 
 Copyright (c) 2025-2026, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 
 
 *****************************************************************************/

#ifndef LL_PADV_SYNC_TRANSFER_COMMON_H
#define LL_PADV_SYNC_TRANSFER_COMMON_H
#ifdef __cplusplus
extern "C"
{
#endif

/*********************************************************************
 * INCLUDES
 */

/*********************************************************************
 * CONSTANTS
 */

// Size of fields in LL_PERIODIC_SYNC_IND LL_PERIODIC_SYNC_WR_IND
#define LL_PAST_CTRL_PACKET_ID_SIZE                             2
#define LL_PAST_CTRL_PACKET_SYNC_INFO_SIZE                      18
#define LL_PAST_CTRL_PACKET_CONN_EVENT_COUNT_SIZE               2
#define LL_PAST_CTRL_PACKET_LAST_PA_EVENT_COUNT_SIZE            2
#define LL_PAST_CTRL_PACKET_SID_ATYPE_SCA_SIZE                  1
#define LL_PAST_CTRL_PACKET_PHY_SIZE                            1
#define LL_PAST_CTRL_PACKET_SYNC_CONN_EVENT_COUNT_SIZE          2
#define LL_PAST_CTRL_PACKET_PAWR_PARAMS_SIZE                    8

// Offsets for LL_PERIODIC_SYNC_IND LL_PERIODIC_SYNC_WR_IND
#define LL_PAST_CTRL_PACKET_ID_OFFSET                           0
#define LL_PAST_CTRL_PACKET_SYNC_INFO_OFFSET                    (LL_PAST_CTRL_PACKET_ID_OFFSET + LL_PAST_CTRL_PACKET_ID_SIZE) // 0+2
#define LL_PAST_CTRL_PACKET_CONN_EVENT_COUNT_OFFSET             (LL_PAST_CTRL_PACKET_SYNC_INFO_OFFSET + LL_PAST_CTRL_PACKET_SYNC_INFO_SIZE) // 2+18
#define LL_PAST_CTRL_PACKET_LAST_PA_EVENT_COUNT_OFFSET          (LL_PAST_CTRL_PACKET_CONN_EVENT_COUNT_OFFSET + LL_PAST_CTRL_PACKET_CONN_EVENT_COUNT_SIZE) // 20+2
#define LL_PAST_CTRL_PACKET_SID_ATYPE_SCA_OFFSET                (LL_PAST_CTRL_PACKET_LAST_PA_EVENT_COUNT_OFFSET + LL_PAST_CTRL_PACKET_LAST_PA_EVENT_COUNT_SIZE) // 22+2
#define LL_PAST_CTRL_PACKET_PHY_OFFSET                          (LL_PAST_CTRL_PACKET_SID_ATYPE_SCA_OFFSET + LL_PAST_CTRL_PACKET_SID_ATYPE_SCA_SIZE) //  24+1
#define LL_PAST_CTRL_PACKET_ADVA_OFFSET                         (LL_PAST_CTRL_PACKET_PHY_OFFSET + LL_PAST_CTRL_PACKET_PHY_SIZE) // 25+1
#define LL_PAST_CTRL_PACKET_SYNC_CONN_EVENT_COUNT_OFFSET        (LL_PAST_CTRL_PACKET_ADVA_OFFSET + B_ADDR_LEN) // 26+6
#define LL_PAST_CTRL_PACKET_SYNC_PAWR_PARAMS_OFFSET              (LL_PAST_CTRL_PACKET_SYNC_CONN_EVENT_COUNT_OFFSET + LL_PAST_CTRL_PACKET_SYNC_CONN_EVENT_COUNT_SIZE) // 32+8

// Bit shifts for SID, AdvA, and SCA
#define LL_PAST_CTRL_PACKET_SCA_BIT_OFFSET                      5  // SCA occupies bits 5-7
#define LL_PAST_CTRL_PACKET_ATYPE_BIT_OFFSET                    4  // AdvA address type occupies bit 4
#define LL_PAST_CTRL_PACKET_SID_BIT_OFFSET                      0  // SID occupies bits 0-3

// Masks for SCA, AdvA, and SID
#define LL_PAST_CTRL_PACKET_SCA_MASK                            0xE0  // Mask for bits 5-7 (3 bits)
#define LL_PAST_CTRL_PACKET_ATYPE_TYPE_MASK                     0x10  // Mask for bit 4 (1 bit)
#define LL_PAST_CTRL_PACKET_SID_MASK                            0x0F  // Mask for bits 0-3 (4 bits)

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * API's FUNCTIONS
 */

#ifdef __cplusplus
}
#endif

#endif /* LL_PADV_SYNC_TRANSFER_COMMON_H */
