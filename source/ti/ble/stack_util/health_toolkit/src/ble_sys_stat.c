
/******************************************************************************

 @file  ble_sys_stat.c

 Group: WCS, BTS
 Target Device: cc23xx

 ******************************************************************************
 
 Copyright (c) 2025, Texas Instruments Incorporated

 All rights reserved not granted herein.
 Limited License.

 Texas Instruments Incorporated grants a world-wide, royalty-free,
 non-exclusive license under copyrights and patents it now or hereafter
 owns or controls to make, have made, use, import, offer to sell and sell
 ("Utilize") this software subject to the terms herein. With respect to the
 foregoing patent license, such license is granted solely to the extent that
 any such patent is necessary to Utilize the software alone. The patent
 license shall not apply to any combinations which include this software,
 other than combinations with devices manufactured by or for TI ("TI
 Devices"). No hardware patent is licensed hereunder.

 Redistributions must preserve existing copyright notices and reproduce
 this license (including the above copyright notice and the disclaimer and
 (if applicable) source code license limitations below) in the documentation
 and/or other materials provided with the distribution.

 Redistribution and use in binary form, without modification, are permitted
 provided that the following conditions are met:

   * No reverse engineering, decompilation, or disassembly of this software
     is permitted with respect to any software provided in binary form.
   * Any redistribution and use are licensed by TI for use only with TI Devices.
   * Nothing shall obligate TI to provide you with source code for the software
     licensed and provided to you in object code.

 If software source code is provided to you, modification and redistribution
 of the source code are permitted provided that the following conditions are
 met:

   * Any redistribution and use of the source code, including any resulting
     derivative works, are licensed by TI for use only with TI Devices.
   * Any redistribution and use of any object code compiled from the source
     code and any resulting derivative works, are licensed by TI for use
     only with TI Devices.

 Neither the name of Texas Instruments Incorporated nor the names of its
 suppliers may be used to endorse or promote products derived from this
 software without specific prior written permission.

 DISCLAIMER.

 THIS SOFTWARE IS PROVIDED BY TI AND TI'S LICENSORS "AS IS" AND ANY EXPRESS
 OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL TI AND TI'S LICENSORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 
 
 *****************************************************************************/

/*******************************************************************************
 * INCLUDES
 */
#include "ti/ble/stack_util/health_toolkit/ble_sys_stat.h"

/*******************************************************************************
 * CONSTANTS
 */
// Bit positions
#define LL_SYSTEM_STATUS_ACTIVE_TASKS_POS 16U
#define LL_SYSTEM_STATUS_NUM_CONN_POS     8U
#define LL_SYSTEM_STATUS_LL_STATE_POS     3U
#define LL_SYSTEM_STATUS_RFU_POS          0U

// Bit masks
#define LL_SYSTEM_STATUS_ACTIVE_TASKS_MASK 0xFFFFU
#define LL_SYSTEM_STATUS_NUM_CONN_MASK     0xFFU
#define LL_SYSTEM_STATUS_LL_STATE_MASK     0x1FU
#define LL_SYSTEM_STATUS_RFU_MASK          0x07U

/*******************************************************************************
 * ENUMS
 */

/*******************************************************************************
 * MACROS
 */

// Packing macro
#define LL_SYSTEM_STATUS_PACK(activeTasks, numConn, llState, rfu)              \
    ((((llSystemStatus_t) (activeTasks) & LL_SYSTEM_STATUS_ACTIVE_TASKS_MASK)  \
      << LL_SYSTEM_STATUS_ACTIVE_TASKS_POS)                                    \
     | (((llSystemStatus_t) (numConn) & LL_SYSTEM_STATUS_NUM_CONN_MASK)        \
        << LL_SYSTEM_STATUS_NUM_CONN_POS)                                      \
     | (((llSystemStatus_t) (llState) & LL_SYSTEM_STATUS_LL_STATE_MASK)        \
        << LL_SYSTEM_STATUS_LL_STATE_POS)                                      \
     | (((llSystemStatus_t) (rfu) & LL_SYSTEM_STATUS_RFU_MASK)                 \
        << LL_SYSTEM_STATUS_RFU_POS))

// Unpacking macros
#define LL_SYSTEM_STATUS_GET_ACTIVE_TASKS(status)                              \
    (((status) >> LL_SYSTEM_STATUS_ACTIVE_TASKS_POS)                           \
     & LL_SYSTEM_STATUS_ACTIVE_TASKS_MASK)
#define LL_SYSTEM_STATUS_GET_NUM_CONN(status)                                  \
    (((status) >> LL_SYSTEM_STATUS_NUM_CONN_POS)                               \
     & LL_SYSTEM_STATUS_NUM_CONN_MASK)
#define LL_SYSTEM_STATUS_GET_LL_STATE(status)                                  \
    (((status) >> LL_SYSTEM_STATUS_LL_STATE_POS)                               \
     & LL_SYSTEM_STATUS_LL_STATE_MASK)
#define LL_SYSTEM_STATUS_GET_ROLE(status)                                      \
    (((status) >> LL_SYSTEM_STATUS_RFU_POS) & LL_SYSTEM_STATUS_RFU_MASK)

// Set field macros (returns a new status variable)
#define LL_SYSTEM_STATUS_SET_ACTIVE_TASKS(status, val)                         \
    (((status)                                                                 \
      & ~(LL_SYSTEM_STATUS_ACTIVE_TASKS_MASK                                   \
          << LL_SYSTEM_STATUS_ACTIVE_TASKS_POS))                               \
     | (((val) & LL_SYSTEM_STATUS_ACTIVE_TASKS_MASK)                           \
        << LL_SYSTEM_STATUS_ACTIVE_TASKS_POS))
#define LL_SYSTEM_STATUS_SET_NUM_CONN(status, val)                             \
    (((status)                                                                 \
      & ~(LL_SYSTEM_STATUS_NUM_CONN_MASK << LL_SYSTEM_STATUS_NUM_CONN_POS))    \
     | (((val) & LL_SYSTEM_STATUS_NUM_CONN_MASK)                               \
        << LL_SYSTEM_STATUS_NUM_CONN_POS))
#define LL_SYSTEM_STATUS_SET_LL_STATE(status, val)                             \
    (((status)                                                                 \
      & ~(LL_SYSTEM_STATUS_LL_STATE_MASK << LL_SYSTEM_STATUS_LL_STATE_POS))    \
     | (((val) & LL_SYSTEM_STATUS_LL_STATE_MASK)                               \
        << LL_SYSTEM_STATUS_LL_STATE_POS))
#define LL_SYSTEM_STATUS_SET_RFU(status, val)                                  \
    (((status) & ~(LL_SYSTEM_STATUS_RFU_MASK << LL_SYSTEM_STATUS_RFU_POS))     \
     | (((val) & LL_SYSTEM_STATUS_RFU_MASK) << LL_SYSTEM_STATUS_RFU_POS))

/*******************************************************************************
 * EXTERNS
 */

/*******************************************************************************
 * GLOBALS
 */
// System Status. Find description in system_status.h
llSystemStatus_t llSystemStatus = 0;

/*******************************************************************************
 * FUNCTIONS
 */

/*******************************************************************************
 * Public function defined in ll_cs_common.h
 */
llSystemStatus_t BleSysStat_getllSysStat(void)
{
    return llSystemStatus;
}

/*******************************************************************************
 * Public function defined in ll_cs_common.h
 */
void BleSysStat_setllActiveTasks(uint16_t activeTasks)
{
    llSystemStatus = LL_SYSTEM_STATUS_SET_ACTIVE_TASKS(llSystemStatus, activeTasks);
}

/*******************************************************************************
 * Public function defined in ll_cs_common.h
 */
void BleSysStat_setllNumConn(uint8_t numConn)
{
    llSystemStatus = LL_SYSTEM_STATUS_SET_NUM_CONN(llSystemStatus, numConn);
}

/*******************************************************************************
 * Public function defined in ll_cs_common.h
 */
void BleSysStat_setllState(uint8_t llState)
{
    llSystemStatus = LL_SYSTEM_STATUS_SET_LL_STATE(llSystemStatus, llState);
}
