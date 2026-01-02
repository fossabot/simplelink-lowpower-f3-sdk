/******************************************************************************

@file  ranging_profile_client.h

@brief This file contains the ranging requestert (RREQ) APIs and structures.

Group: WCS, BTS
Target Device: cc23xx

******************************************************************************

 Copyright (c) 2025, Texas Instruments Incorporated
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

#ifndef RANGING_PROFILE_CLIENT_H
#define RANGING_PROFILE_CLIENT_H

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef RANGING_CLIENT
/*********************************************************************
 * INCLUDES
 */
#include "ti/ble/stack_util/bcomdef.h"
#include "ti/ble/services/ranging/ranging_types.h"
#include "ti/ble/profiles/ranging/ranging_db_client.h"

/*********************************************************************
*  EXTERNAL VARIABLES
*/

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * TYPEDEFS
 */

typedef void (*RREQ_DataReadyCallback)(uint16_t connHandle, uint16_t rangingCount);
typedef void (*RREQ_SubeventDataCallback)(uint16_t connHandle, uint16_t rangingCount, void *pCSSubEvent);
typedef void (*RREQ_CompleteEventCallback)(uint16_t connHandle, uint16_t rangingCount, uint8_t status, RangingDBClient_procedureSegmentsReader_t segmentsReader);
typedef void (*RREQ_StatusCallback)(uint16_t connHandle, uint8_t statusCode, uint8_t statusDataLen, uint8_t* statusData);

/*********************************************************************
 * Structures
 */

 // RREQ Configuration Subscription types
typedef enum
{
    RREQ_DISABLE_NOTIFY_INDICATE = 0x00,  // Disable notifications and indications
    RREQ_PREFER_NOTIFY,                   // Prefer notifications over indications
    RREQ_INDICATE,                        // Use indications for notifications
} RREQConfigSubType_e;

// RREQ Enable modes
typedef enum
{
    RREQ_MODE_NONE = 0x00,  // data exchange mode not set
    RREQ_MODE_ON_DEMAND,    // On-demand ranging request
    RREQ_MODE_REAL_TIME,    // Real-time ranging request
} RREQEnableModeType_e;

// RREQ On-Demand configuration structure
typedef struct
{
    RREQConfigSubType_e onDemandSubType;       // On-demand data subscription type
    RREQConfigSubType_e controlPointSubType;   // Control point subscription type
    RREQConfigSubType_e dataReadySubType;      // Data ready event subscription type
    RREQConfigSubType_e overwrittenSubType;    // Data overwritten event subscription type
} RREQOnDemandSubConfig_t;

// RREQ Real-Time configuration structure
typedef struct
{
    RREQConfigSubType_e realTimeSubType;       // Real-time data subscription type
} RREQRealTimeSubConfig_t;

// RREQ Timeout configuration structure
typedef struct
{
    uint32_t timeOutFirstSegment;   // Timeout for receiving first segment (Real-Time only)
    uint32_t timeOutNextSegment;    // Timeout for next segment event in milliseconds
} RREQTimeoutConfig_t;

// RREQ Configuration structure
typedef struct
{
    RREQOnDemandSubConfig_t onDemandSubConfig;  // Subscriptions configuration for On-Demand
    RREQRealTimeSubConfig_t realTimeSubConfig;  // Subscriptions configuration for Real-Time
    RREQTimeoutConfig_t timeoutConfig;      // Timeout configurations
} RREQConfig_t;

// RREQ Callbacks structure
typedef struct
{
    RREQ_DataReadyCallback pDataReadyCallback;              // Callback for data ready events
    RREQ_CompleteEventCallback pDataCompleteEventCallback;  // Callback for complete events
    RREQ_StatusCallback pStatusCallback;                    // Callback for status events
} RREQCallbacks_t;

// RREQ Client Status Codes
typedef enum
{
  RREQ_TIMEOUT = 0x00,            // Timeout occurred during the procedure
  RREQ_ABORTED_SUCCESSFULLY,      // Procedure aborted successfully
  RREQ_ABORTED_UNSUCCESSFULLY,    // Procedure aborted unsuccessfully
  RREQ_SERVER_BUSY,               // Server is busy, cannot process the request
  RREQ_PROCEDURE_NOT_COMPLETED,   // Procedure not completed successfully
  RREQ_NO_RECORDS,                // No records found for the request
  RREQ_DATA_INVALID,              // Data received is invalid
  RREQ_DATA_OVERWRITTEN,          // Data has been overwritten
} RREQClientStatus_e;

/*********************************************************************
 * FUNCTIONS
 */

 /*********************************************************************
 * @fn      RREQ_Start
 *
 * @brief   Initializes the ranging requester and
 *          stores the provided configuration and callbacks.
 *
 * @note    This function should be called once.
 *
 * input parameters
 *
 * @param   pCallbacks - Pointer to the callback structure.
 * @param   pConfig - Pointer to the configuration structure.
 *
 * output parameters
 *
 * @param   None
 *
 * @return  SUCCESS - if the initialization was successful.
 *          INVALIDPARAMETER - if the input parameters are invalid,
 *                             or if the RREQ already started
 *
 */
uint8_t RREQ_Start(const RREQCallbacks_t *pCallbacks , const RREQConfig_t *pConfig);

/*********************************************************************
 * @fn      RREQ_Enable
 *
 * @brief   Enables the RREQ process.
 *          This function start the RREQ process by discovering the RAS (Ranging Service)
 *          service on the specified connection handle.
 *          when enableMode is set to @RREQ_REAL_TIME, a registration
 *          for the Real-Time characteristic will be done as long as the
 *          server allows it, otherwise - will register to the relevant
 *          On-Demand characteristics.
 *
 * @note    For changing data exchange modes when RREQ is already enabled,
 *          consider using @ref RREQ_ConfigureCharRegistration API, instead
 *          of using @ref RREQ_Disable.
 *
 * input parameters
 *
 * @param   connHandle - Connection handle.
 * @param   enableMode - Preferred mode to enable: @ref RREQ_ON_DEMAND or @ref RREQ_REAL_TIME
 *
 * output parameters
 *
 * @param   None
 *
 * @return  return SUCCESS or an error status indicating the failure reason.
 */
uint8_t RREQ_Enable(uint16_t connHandle, RREQEnableModeType_e enableMode);

/*********************************************************************
 * @fn      RREQ_Disable
 *
 * @brief   Disable the RREQ process.
 *          This function start the RREQ process by discovering the RAS (Ranging Service)
 *          service on the specified connection handle
 *
 * input parameters
 *
 * @param   connHandle - The connection handle for the RAS service.
 *
 * output parameters
 *
 * @param   None
 *
 * @return SUCCESS - if the RREQ process was successfully disabled.
 *         INVALIDPARAMETER - if the connection handle is invalid.
 */
uint8_t RREQ_Disable(uint16_t connHandle);

/*********************************************************************
 * @fn      RREQ_ConfigureCharRegistration
 *
 * @brief Configures the registration for a characteristic in the Ranging Profile Client.
 *
 * This function registers or unregisters for notifications or indications on a specific
 * characteristic identified by its UUID for a given connection handle.
 *
 * @param connHandle   The connection handle identifying the BLE connection.
 * @param charUUID     The UUID of the characteristic to configure.
 * @param subType      The type of configuration to apply (see RREQConfigSubType_e).
 *
 * @note Does not accept RAS_FEATURE_UUID for configuration.
 *
 * @return SUCCESS - if the configuration was successful.
 * @return INVALIDPARAMETER - if the connection handle is invalid, the characteristic
 *                            UUID is not supported, subType is not valid, the service
 *                            has not been discovered yet, or if trying to register to
 *                            both Real-Time and On-Demand characteristics.
 * @return status derived by @ref GATT_WriteCharValue function in case of executing the configuration.
 */
uint8_t RREQ_ConfigureCharRegistration(uint16_t connHandle, uint16_t charUUID, RREQConfigSubType_e subType);

/*********************************************************************
 * @fn      RREQ_GetRangingData
 *
 * @brief   Starts the process of reading data for a ranging request.
 *          This function initiates the data reading process for a specified connection
 *          handle and ranging count.
 *
 * input parameters
 *
 * @param   connHandle  - Connection handle.
 * @param   RangingCount - CS procedure counter.
 *
 * output parameters
 *
 * @param   None
 *
 * @return return SUCCESS or an error status indicating the failure reason.
 */
uint8_t RREQ_GetRangingData(uint16_t connHandle, uint16_t rangingCount);

/*********************************************************************
 * @fn      RREQ_Abort
 *
 * @brief   Aborts the ongoing ranging request.
 *          This function is responsible for aborting the ongoing ranging request
 *          for a specified connection handle.
 *          The function ensures that the On-Demand is being used and that
 *          the server supports an abort operation.
 *
 * input parameters
 *
 * @param   connHandle - Connection handle.
 *
 * output parameters
 *
 * @param   None
 *
 * @return return SUCCESS or an error status indicating the failure reason.
 */
uint8_t RREQ_Abort(uint16_t connHandle);

/*********************************************************************
 * @fn      RREQ_ProcedureStarted
 *
 * @brief API to notify the RREQ profile that a Channel Sounding procedure
 *        has started.
 *        Relevant for Real-Time mode only.
 *        The function won't take any action if the given procedure counter
 *        is the same as the last notified one.
 *
 * @param connHandle - Connection handle.
 * @param procedureCounter - CS procedure counter that has been started.
 *
 * @return SUCCESS
 * @return INVALIDPARAMETER - if the connection handle is invalid.
 * @return bleIncorrectMode - if Real-Time mode is not enabled or
 *                            another procedure is already in progress.
 * @return bleAlreadyInRequestedMode - if the given procedure counter
 *                                     is the same as the last notified one.
 */
uint8_t RREQ_ProcedureStarted(uint16_t connHandle, uint16_t procedureCounter);

#endif // RANGING_CLIENT

#ifdef __cplusplus
}
#endif

#endif /* RANGING_PROFILE_CLIENT_H */
