/******************************************************************************

 @file  ble_sys_stat.h

 @brief Manages the BLE system status variables.

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
#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

/*******************************************************************************
 * INCLUDES
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/*******************************************************************************
 * CONSTANTS
 */

/*******************************************************************************
 * ENUMS
 */

/*******************************************************************************
 * MACROS
 */

/*******************************************************************************
 * EXTERNS
 */

/*******************************************************************************
 * TYPEDEFS
 */

/******************************************************************************
 * System Status Variable
 *****************************************************************************
 * Bit Layout (MSB -> LSB):
 * 31-16: 16bit: Number of active task
 * 15-8: 8bit: Number of connections (uint8_t, 0-255)
 * 7-3: 5bit: Link Layer state
 * 2-0: RFU
 */
typedef uint32_t llSystemStatus_t;

/*******************************************************************************
 * FUNCTIONS
 */

/*******************************************************************************
 * @fn          BleSysStat_getllSysStat
 *
 * @brief       Retrieves the current packed system status value.
 *
 * input parameters
 *
 * @param       None
 *
 * output parameters
 *
 * @param       None.
 *
 * @return      The current system status as a 32-bit packed value.
 */
llSystemStatus_t BleSysStat_getllSysStat(void);

/*******************************************************************************
 * @fn          BleSysStat_setllActiveTasks
 *
 * @brief       Updates the active tasks in the system status.
 *
 * input parameters
 *
 * @param       activeTasks - Bitwise representation of active tasks.
 *
 * output parameters
 *
 * @param       None.
 *
 * @return      None
 */
void BleSysStat_setllActiveTasks(uint16_t activeTasks);

/*******************************************************************************
 * @fn          BleSysStat_setllNumConn
 *
 * @brief       Updates the number of BLE connections in the system status.
 *
 * input parameters
 *
 * @param       numConn - The number of BLE connections to set.
 *
 * output parameters
 *
 * @param       None.
 *
 * @return      None
 */
void BleSysStat_setllNumConn(uint8_t numConn);

/*******************************************************************************
 * @fn          BleSysStat_setllState
 *
 * @brief       Updates the Link Layer (LL) state in the system status.
 *
 * input parameters
 *
 * @param       llState - The new LL state value to set.
 *
 * output parameters
 *
 * @param       None.
 *
 * @return      None
 */
void BleSysStat_setllState(uint8_t llState);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_STATUS_H */