/******************************************************************************

@file  ranging_profile_client.c

@brief This file contains the ranging requester (RREQ) APIs and functionality.

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

#ifdef RANGING_CLIENT
//*****************************************************************************
//! Includes
//*****************************************************************************
#include <string.h>
#include "ti/ble/app_util/common/util.h"
#include "ti/ble/host/gatt/gatt.h"
#include "ti/ble/app_util/framework/bleapputil_api.h"
#include "ti/ble/app_util/framework/bleapputil_timers.h"
#include "ti/ble/profiles/ranging/ranging_profile_client.h"
#include "app_gatt_api.h"

//*****************************************************************************
//! Defines
//*****************************************************************************

// The index of characteristic properties handle
#define RREQ_APP_CHAR_PRO_HANDLE_INDEX              0x02
// The index of characteristic value handle
#define RREQ_APP_CHAR_VALUE_HANDLE_INDEX            0x03
// The index of characteristic UUID handle
#define RREQ_APP_CHAR_UUID_HANDLE_INDEX             0x05

// Profile number of characteristics
#define RREQ_NUM_CHARS                              6U

// Maps characteristic UUID to its array index
#define RREQ_UUID_TO_CHAR_INDEX(uuid)               ((uint8_t) ((uuid) - RAS_FEATURE_UUID))

// Maps characteristic UUID to its subscription bit
// Warning: Do not use for @ref RAS_FEATURE_UUID
#define RREQ_UUID_TO_CHAR_SUBSCRIBE_BIT(uuid)    ((uint8_t) (BV(RREQ_UUID_TO_CHAR_INDEX(uuid) - 1)))

#define RREQ_FEATURE_CHAR_INDEX                     0U
#define RREQ_REAL_TIME_CHAR_INDEX                   1U
#define RREQ_ON_DEMAND_CHAR_INDEX                   2U
#define RREQ_CONTROL_POINT_CHAR_INDEX               3U
#define RREQ_DATA_READY_CHAR_INDEX                  4U
#define RREQ_DATA_OVERWRITTEN_CHAR_INDEX            5U

// 16-bit high and low index in the 16-bit custom UUID
#define RREQ_APP_LOW_UUID_INDEX                     0x00
#define RREQ_APP_HIGH_UUID_INDEX                    0x01

// CCCD handle offset
#define RREQ_APP_CCCD_OFFSET                        0x01
// CCCD value length
#define RREQ_APP_CCCD_VALUE_LEN                     0x02

// The maximum number of connections for the RAS client
#define RREQ_MAX_CONN                               MAX_NUM_BLE_CONNS

#define RREQ_INVALID_CS_PROCEDURE_COUNTER           0xFFFFFFFF

// Notification and Indication properties bit mask
#define RAS_NOTIFICATION_PRO_MASK                   0x10
#define RAS_INDICATION_PRO_MASK                     0x20

// segmentation bit masks
#define RAS_FIRST_SEGMENT_BIT_MASK                  0x01
#define RAS_LAST_SEGMENT_BIT_MASK                   0x02

// Last 6 bits (LSB) of uint8_t
#define RAS_LAST_6_BITS_LSB(x)                      ((x & 0xFc) >> 2)
// First 2 bits (LSB) of uint8_t
#define RAS_FIRST_2_BITS_LSB(x)                     (x & 0x03)

/*********************************************************************
 * TYPEDEFS
 */

// RREQ Procedure States
typedef enum
{
    RREQ_STATE_IDLE = 0x00,                 // Idle state
    RREQ_STATE_INIT,                        // Waiting for RAS Enable process to end. This state is relevant for initiating RAS discovery and full registration
    RREQ_STATE_DISCOVER_PRIM_SERVICE,       // waiting for primary service discovery response
    RREQ_STATE_READ_CHAR_FEATURE,           // waiting for reading feature characteristic response
    RREQ_STATE_WAIT_FOR_FIRST_SEGMENT,      // Waiting for the first segment, relevant for Real-Time mode only
    RREQ_STATE_WAIT_FOR_NEXT_SEGMENT,       // Waiting for the next segment or the complete data
    RREQ_STATE_WAIT_FOR_CONTROL_POINT_RSP,  // Waiting for the control point response
    RREQ_STATE_WAIT_FOR_ABORT               // Waiting for the abort response
} RREQProcedureStates_e;

// RREQ Subscription types
typedef enum
{
    RREQ_REAL_TIME_BIT      = (uint32_t)BV(0),  // Real-time ranging data subscription
    RREQ_ON_DEMAND_BIT      = (uint32_t)BV(1),  // On-demand ranging data subscription
    RREQ_CONTROL_POINT_BIT  = (uint32_t)BV(2),  // Control point command subscription
    RREQ_DATA_READY_BIT     = (uint32_t)BV(3),  // Data ready event subscription
    RREQ_OVERWRITTEN_BIT    = (uint32_t)BV(4)   // Data overwritten event subscription
}RREQ_SubscribeBitMap_e;

// RREQ characteristics structure
typedef struct
{
    uint16_t charHandle;
    uint16_t charProperties;
} RREQ_CharInfo_t ;

// RREQ Procedure Attribute structure
typedef struct
{
    uint8_t procedureState;   // Current state of the RREQ procedure
    uint16_t rangingCounter;  // Counter for the ranging procedure
} RREQProcedureAttr_t;

// RREQ Segments Manager structure
typedef struct
{
    uint8_t  lastSegmentFlag;   // Flag to indicate if the last segment is received.
} RREQSegmentsMGR_t;

typedef struct
{
    uint16_t startHandle;                           // Start handle of the service
    uint16_t endHandle;                             // End handle of the service
    uint16_t featureCharValue;                      // Feature characteristic value
    uint32_t currentProcedureCounter;               // CS procedure counter of the currently running procedure.
    RREQ_CharInfo_t charInfo[RREQ_NUM_CHARS];       // Characteristics information array, ordered according to UUID
    uint8_t  subscribeBitMap;                       // Subscribtion bit map
    RREQProcedureAttr_t procedureAttr;              // Procedure attributes
    RREQEnableModeType_e preferredMode;             // Preferred mode to be enabled if possible
    RREQEnableModeType_e enableMode;                // Enable mode
    RREQSegmentsMGR_t    segmentMgr;                // Segments manager
    BLEAppUtil_timerHandle timeoutHandle;           // Timer handle for the RREQ timeout
} RREQ_ConnInfo_t;

typedef struct
{
    RREQ_ConnInfo_t connInfo[RREQ_MAX_CONN];  // Connection information
    const RREQConfig_t* config;               // Configuration for the RREQ
    const RREQCallbacks_t* callbacks;         // Application callbacks
} RREQ_ControlBlock_t;

/*********************************************************************
 * CONSTANTS
 */

// Length of the Ranging Counter
#define RANGING_COUNTER_LEN 2

//*****************************************************************************
//! Globals

// RREQ control block
RREQ_ControlBlock_t gRREQControlBlock = {0};

//*****************************************************************************
//!LOCAL FUNCTIONS
//*****************************************************************************
static void rreq_procedureStarted(uint16_t connHandle, uint16_t procedureCounter);
static bStatus_t rreq_discoverAllChars(uint16_t connHandle, uint16_t startHandle, uint16_t endHandle);
static RREQConfigSubType_e rreq_getCharProperties(RREQConfigSubType_e mode, uint8_t properties);
static bStatus_t rreq_discoverPrimServ(uint16_t connHandle);
static void rreq_handleFindByTypeValueRsp(gattMsgEvent_t *gattMsg);
static void rreq_handleReadByTypeRsp(gattMsgEvent_t *gattMsg);
static void rreq_handleValueNoti(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti);
static void rreq_handleOnDemandValueNoti(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti);
static void rreq_handleRealTimeValueNoti(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti);
static void rreq_handleWriteRsp(gattMsgEvent_t *gattMsg);
static void rreq_handleReadRsp(gattMsgEvent_t *gattMsg);
static bStatus_t rreq_sendControlPointWriteCmd(uint16_t connHandle, uint8_t cmd, uint16_t rangingCounter, uint8_t len);
static bool rreq_checkRegistration(RREQConfigSubType_e subNeeded, uint8_t subscribeBitMap, RREQ_SubscribeBitMap_e subscribeBit);
static bStatus_t rreq_registerConfigChars(uint16_t connHandle);
static bStatus_t rreq_registerOnDemandConfigChars(uint16_t connHandle);
static bStatus_t rreq_registerRealTimeConfigChars(uint16_t connHandle);
static bStatus_t rreq_configureCharRegistration(uint16_t connHandle, uint16_t charUUID, RREQConfigSubType_e mode);
static bStatus_t rreq_registerCharacteristic(uint16_t connHandle, uint16_t charUUID, RREQConfigSubType_e mode);
static bStatus_t rreq_unregisterCharacteristic(uint16_t connHandle, uint16_t charUUID);
static bStatus_t rreq_enableNotification(uint16_t connHandle, RREQConfigSubType_e mode, uint16_t attHandle);
static void rreq_discoverAllCharDescriptors(uint16_t connHandle);
static void rreq_clearData(uint16_t connHandle);
static void clearSegmentMgr(uint16_t connHandle);
static void rreq_readFeaturesCharValue(uint16_t connHandle);
static void rreq_handleControlPointRsp(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti);
static void rreq_handleOnDemandSegmentReceived(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti);
static void rreq_handleRealTimeSegmentReceived(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti);
static void rreq_parseSegmentReceived(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti);
static void rreq_handleRspCode(uint16_t connHandle, uint8_t rspValue);
static void rreq_procedureDone(uint16_t connHandle);

// Timer functions
static void rreq_startTimer( uint32_t timeout, uint16_t connHandle );
static void rreq_stopTimer( uint16_t connHandle );
static void rreq_timerCB(BLEAppUtil_timerHandle timerHandle, BLEAppUtil_timerTermReason_e reason, void *pData);

//*****************************************************************************
//!LOCAL VARIABLES
//*****************************************************************************

//*****************************************************************************
//!APPLICATION CALLBACK
//*****************************************************************************
static void RREQ_GATTEventHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData);

// Events handlers struct, contains the handlers and event masks
// of the application data module
BLEAppUtil_EventHandler_t RREQGATTHandler =
{
    .handlerType    = BLEAPPUTIL_GATT_TYPE,
    .pEventHandler  = RREQ_GATTEventHandler,
    .eventMask      = BLEAPPUTIL_ATT_FIND_BY_TYPE_VALUE_RSP   |
                      BLEAPPUTIL_ATT_READ_BY_TYPE_RSP         |
                      BLEAPPUTIL_ATT_HANDLE_VALUE_IND         |
                      BLEAPPUTIL_ATT_HANDLE_VALUE_NOTI        |
                      BLEAPPUTIL_ATT_WRITE_RSP                |
                      BLEAPPUTIL_ATT_READ_RSP                 |
                      BLEAPPUTIL_ATT_ERROR_RSP                |
                      BLEAPPUTIL_ATT_FIND_INFO_RSP
};

//*****************************************************************************
//! Functions
//*****************************************************************************

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*******************************************************************************
 * Public function defined in ranging_profile_client.h.
 */
uint8_t RREQ_Start(const RREQCallbacks_t *pCallbacks , const RREQConfig_t *pConfig)
{
    uint8_t status = SUCCESS;

    // Check input parameters and check if this function already called once
    if ( pCallbacks == NULL ||
         pConfig == NULL ||
         gRREQControlBlock.config != NULL ||
         gRREQControlBlock.callbacks != NULL)
    {
        return INVALIDPARAMETER;
    }

    // Save configuration and callbacks
    gRREQControlBlock.config = pConfig;
    gRREQControlBlock.callbacks = pCallbacks;

    // Clear all connections data
    for (uint16_t connHandle = 0; connHandle < RREQ_MAX_CONN; connHandle++)
    {
        rreq_clearData(connHandle);
    }

    // Register the GATT event handler
    status = BLEAppUtil_registerEventHandler( &RREQGATTHandler );

    if ( status == SUCCESS )
    {
        // Init ranging client DB
        status = RangingDBClient_initDB();
    }

    return status;
}


/*******************************************************************************
 * Public function defined in ranging_profile_client.h.
 */
uint8_t RREQ_Enable(uint16_t connHandle, RREQEnableModeType_e enableMode)
{
    uint8_t status = SUCCESS;

    // Check if the connection handle is valid and the enable mode is supported
    if (connHandle >= RREQ_MAX_CONN ||
        (enableMode != RREQ_MODE_ON_DEMAND && enableMode != RREQ_MODE_REAL_TIME))
    {
        status = INVALIDPARAMETER;
    }
    else
    {
        // Check if the RREQ is already enabled for this connection handle
        if (gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_NONE)
        {
            // Set the preferred mode as given by the caller
            gRREQControlBlock.connInfo[connHandle].preferredMode = enableMode;

            // Set the current state to Init
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_INIT;

            // Check if need to discover the RAS service
            if(gRREQControlBlock.connInfo[connHandle].endHandle == 0)
            {
                // Open ranging client DB per connection handle
                status = RangingDBClient_procedureOpen(connHandle);

                if(status == SUCCESS)
                {
                    // Discover the RAS service
                    status = rreq_discoverPrimServ(connHandle);
                }

                if (status != SUCCESS)
                {
                    // Abort and clear data
                    rreq_clearData(connHandle);

                    // Clear the DB in case it was opened
                    RangingDBClient_procedureClose(connHandle);
                }
            }
            else
            {
                // register to the server characteristics
                status = rreq_registerConfigChars(connHandle);
            }
        }
        else
        {
            // Already enabled
            status = INVALIDPARAMETER;
        }
    }
    return status;
}

/*******************************************************************************
 * Public function defined in ranging_profile_client.h.
 */
uint8_t RREQ_Disable(uint16_t connHandle)
{
    uint8_t status = INVALIDPARAMETER;

    // Check if the connection handle is valid
    if (connHandle < RREQ_MAX_CONN)
    {
        // Unregister all peer notifications/indications
        for (uint16_t charUUID = RAS_REAL_TIME_UUID; charUUID <= RAS_DATA_OVERWRITTEN_UUID; charUUID++)
        {
            rreq_configureCharRegistration(connHandle, charUUID, RREQ_DISABLE_NOTIFY_INDICATE);
        }

        // Clear all connHandle saved data
        rreq_clearData(connHandle);

        // Stop the timer in case it's running
        rreq_stopTimer(connHandle);

        // Close the ranging client DB
        RangingDBClient_procedureClose(connHandle);

        status = SUCCESS;
    }

    return status;
}

/*******************************************************************************
 * Public function defined in ranging_profile_client.h.
 */
uint8_t RREQ_ConfigureCharRegistration(uint16_t connHandle, uint16_t charUUID, RREQConfigSubType_e subType)
{
    uint8_t status = SUCCESS;

    // Check connection handle
    // Make sure that a discovery of the service has been completed
    if (connHandle >= RREQ_MAX_CONN ||
        subType > RREQ_INDICATE ||
        gRREQControlBlock.connInfo[connHandle].endHandle == 0)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        switch(charUUID)
        {
            case RAS_REAL_TIME_UUID:
            case RAS_ON_DEMAND_UUID:
            case RAS_CONTROL_POINT_UUID:
            case RAS_DATA_READY_UUID:
            case RAS_DATA_OVERWRITTEN_UUID:
            {
                status = rreq_configureCharRegistration(
                    connHandle,
                    charUUID,
                    subType
                );

                break;
            }

            case RAS_FEATURE_UUID:
            default:
            {
                status = INVALIDPARAMETER;
                break;
            }
        }
    }

    return status;
}

/*******************************************************************************
 * Public function defined in ranging_profile_client.h.
 */
uint8_t RREQ_GetRangingData(uint16_t connHandle, uint16_t rangingCount)
{
    uint8_t status = SUCCESS;

    // Check if the connection handle is valid, the configuration is not NULL
    // and that the enabled mode is On-Demand
    if ((connHandle >= RREQ_MAX_CONN) || (gRREQControlBlock.config == NULL) ||
        (gRREQControlBlock.connInfo[connHandle].enableMode != RREQ_MODE_ON_DEMAND))
    {
        return INVALIDPARAMETER;
    }

    // Check if the procedure state is idle
    if (gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState == RREQ_STATE_IDLE)
    {
        // Send write command to control point characteristic handle
        status = rreq_sendControlPointWriteCmd(connHandle, RAS_CP_OPCODE_GET_RANGING_DATA, rangingCount, RAS_CP_GET_DATA_CMD_LEN);

        // Init the procedure Attr
        if(status == SUCCESS)
        {
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_WAIT_FOR_NEXT_SEGMENT;
            gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter = rangingCount;

            // Start the timer for receiving first segment
            rreq_startTimer(gRREQControlBlock.config->timeoutConfig.timeOutFirstSegment, connHandle);
        }
        else
        {
            // Reset the procedure state if the command failed
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
        }
    }
    else
    {
        // The procedure is already in progress
        status = bleIncorrectMode;
    }

    return status;
}

/*******************************************************************************
 * Public function defined in ranging_profile_client.h.
 */
uint8_t RREQ_Abort(uint16_t connHandle)
{
    uint8_t status = SUCCESS;

    // Check if the connection handle is valid
    if (connHandle >= RREQ_MAX_CONN)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        // Ensure:
        // 1. Abort operation is allowed
        // 2. Enabled mode is On-Demand
        // 3. Procedure is in progress
        if ((gRREQControlBlock.connInfo[connHandle].featureCharValue & RAS_FEATURES_ABORT_OPERATION) &&
            gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_ON_DEMAND &&
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState == RREQ_STATE_WAIT_FOR_NEXT_SEGMENT)
        {
            // Send Abort command to the server
            status = rreq_sendControlPointWriteCmd(connHandle, RAS_CP_OPCODE_ABORT_OPERATION,
                                                   gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter,
                                                   RAS_CP_ABORT_CMD_LEN);
            if( status == SUCCESS)
            {
                // Reset the procedure state to wait for abort response
                gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_WAIT_FOR_ABORT;
            }
        }
        else
        {
            status = bleDisallowed;
        }
    }

    return status;
}

/*******************************************************************************
 * Public function defined in ranging_profile_client.h.
 */
uint8_t RREQ_ProcedureStarted(uint16_t connHandle, uint16_t procedureCounter)
{
    // Ensure the connection handle is valid
    if (connHandle >= RREQ_MAX_CONN)
    {
        return INVALIDPARAMETER;
    }

    // Ensure Real-Time mode is enabled and that no other procedure is in progress
    if (gRREQControlBlock.connInfo[connHandle].enableMode != RREQ_MODE_REAL_TIME ||
        gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState != RREQ_STATE_IDLE)
    {
        return bleIncorrectMode;
    }

    // Ensure that the procedure counter is different than the last notified one
    if (gRREQControlBlock.connInfo[connHandle].currentProcedureCounter == procedureCounter)
    {
        return bleAlreadyInRequestedMode;
    }

    rreq_procedureStarted(connHandle, procedureCounter);

    return SUCCESS;
}

 /*********************************************************************
 * LOCAL FUNCTIONS
 */

/*********************************************************************
 * @fn      RREQ_GATTEventHandler
 *
 * @brief   The purpose of this function is to handle GATT events
 *          that rise from the GATT and were registered in
 *          @ref BLEAppUtil_RegisterGAPEvent
 *
 * input parameters
 *
 * @param   event - message event.
 * @param   pMsgData - pointer to message data.
 *
 * output parameters
 *
 * @param   None
 *
 * @return  none
 */
static void RREQ_GATTEventHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData)
{
    gattMsgEvent_t *gattMsg = ( gattMsgEvent_t * )pMsgData;

    // Check if the connection handle valid
    if ( gattMsg->connHandle >= RREQ_MAX_CONN )
    {
        return;
    }
    switch ( event )
    {
        case BLEAPPUTIL_ATT_FIND_BY_TYPE_VALUE_RSP:
        {
            // Handle the find by type value response
            rreq_handleFindByTypeValueRsp( gattMsg );
            break;
        }

        case BLEAPPUTIL_ATT_READ_BY_TYPE_RSP:
        {
            // Handle the read by type response
            rreq_handleReadByTypeRsp( gattMsg );
            break;
        }

        case BLEAPPUTIL_ATT_HANDLE_VALUE_NOTI:
        {
            // Handle the notification
            rreq_handleValueNoti( gattMsg->connHandle, &(gattMsg->msg.handleValueNoti) );
            break;
        }

        case BLEAPPUTIL_ATT_HANDLE_VALUE_IND:
        {
            // Send an indication confirmation
            ATT_HandleValueCfm(gattMsg->connHandle);

            // Handle the indication
            attHandleValueNoti_t handleValueNoti =
            {
             .handle = gattMsg->msg.handleValueInd.handle,
             .len    = gattMsg->msg.handleValueInd.len,
             .pValue = gattMsg->msg.handleValueInd.pValue
            };
            rreq_handleValueNoti( gattMsg->connHandle, &handleValueNoti );
            break;
        }

        case BLEAPPUTIL_ATT_WRITE_RSP:
        {
            // Handle the write response
            rreq_handleWriteRsp( gattMsg );
            break;
        }

        case BLEAPPUTIL_ATT_ERROR_RSP:
        {
          // TODO: implement ATT ERROR handling
            break;
        }

        case BLEAPPUTIL_ATT_READ_RSP:
        {
            // Handle the read response
            rreq_handleReadRsp( gattMsg );
            break;
        }

        case BLEAPPUTIL_ATT_FIND_INFO_RSP:
        {
            // TODO: parse the find info response to find handles ccc.
            if( gattMsg->hdr.status == bleProcedureComplete )
            {
                // read from "read feature" characteristic
                rreq_readFeaturesCharValue(gattMsg->connHandle);
            }
        }

        default:
            break;
    }
}

/*********************************************************************
 * @fn      rreq_procedureStarted
 *
 * @brief   Internal function to notify the RREQ profile that a
 *          Channel Sounding procedure has started. Relevant for Real-Time
 *          mode only.
 *          The function will change the RREQ state accordingly for
 *          the specified connection handle.
 *
 * input parameters
 *
 * @param   connHandle - Connection handle.
 * @param   procedureCounter - CS procedure counter that has been started.
 *
 * @note    Assumes that the procedureCounter parameter is different
 *          than the last notified one.
 *
 * @return  none
 */
static void rreq_procedureStarted(uint16_t connHandle, uint16_t procedureCounter)
{
    if (connHandle >= RREQ_MAX_CONN)
    {
        return;
    }

    // Keep track of the CS procedure counter for this connection
    gRREQControlBlock.connInfo[connHandle].currentProcedureCounter = procedureCounter;

    // Set the state to wait for the first segment and update the ranging counter
    gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_WAIT_FOR_FIRST_SEGMENT;
    gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter = procedureCounter;

    // Start the timer for timeout
    rreq_startTimer(gRREQControlBlock.config->timeoutConfig.timeOutFirstSegment, connHandle);
}

/*********************************************************************
 * @fn      rreq_discoverAllChars
 *
 * @brief   Car Access client discover all service characteristics,
 *          ATT_READ_BY_TYPE_RSP
 *
 * input parameters
 *
 * @param   connHandle - connection message was received on
 *
 * output parameters
 *
 * @param   None
 *
 * @return  SUCCESS or stack call status
 */
static bStatus_t rreq_discoverAllChars(uint16_t connHandle, uint16_t startHandle, uint16_t endHandle)
{
    bStatus_t status = INVALIDPARAMETER;

    // Discovery simple service
    if (connHandle < RREQ_MAX_CONN)
    {
        status = GATT_DiscAllChars(connHandle, startHandle, endHandle, BLEAppUtil_getSelfEntity());
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_readFeaturesCharValue
 *
 * @brief   Reads the value of a ras feature characteristic from a remote device.
 *          This function initiates a read request for the value of a feature char
 *          identified by its handle on a remote device. The result of the read operation
 *          will be provided asynchronously through event ATT_READ_RSP.
 *
 * input parameters
 *
 * @param   connHandle  - Connection handle.
 *
 * output parameters
 *
 * @param   None
 *
 * @return Status code indicating the success or failure of the operation.
 */
static void rreq_readFeaturesCharValue(uint16_t connHandle)
{
    attReadReq_t req;

    // Check if the connection handle is valid and the procedure state is INIT
    if ( (connHandle < RREQ_MAX_CONN) && (gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState == RREQ_STATE_INIT))
    {
        // Update the procedure state to indicate that we are reading the characteristic feature
        gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_READ_CHAR_FEATURE;

        // Read the characteristic value
        req.handle = gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_FEATURE_CHAR_INDEX].charHandle;
        GATT_ReadCharValue(connHandle, &req, BLEAppUtil_getSelfEntity());
    }
}

/*********************************************************************
 * @fn      rreq_handleWriteRsp
 *
 * @brief   The purpose of this function is to handle @ref BLEAPPUTIL_ATT_WRITE_RSP
 *          events that rise from the GATT.
 *
 * input parameters
 *
 * @param   gattMsg - pointer to the GATT message event structure
 *
 * output parameters
 *
 * @param   None
 *
 * @return  none
 */
static void rreq_handleWriteRsp(gattMsgEvent_t *gattMsg)
{
    if(gattMsg != NULL &&
       gattMsg->hdr.status == SUCCESS &&
       gattMsg->connHandle < RREQ_MAX_CONN)
    {
        // If currently on Init state, keep reading characteristics
        if (gRREQControlBlock.connInfo[gattMsg->connHandle].procedureAttr.procedureState == RREQ_STATE_INIT)
        {
            // register to the server characteristics
            rreq_registerConfigChars(gattMsg->connHandle);
        }
    }
}

/*********************************************************************
 * @fn      rreq_handleReadRsp
 *
 * @brief   The purpose of this function is to handle @ref BLEAPPUTIL_ATT_READ_RSP
 *          events that rise from the GATT.
 *          Relevant when in INIT state.
 *
 * input parameters
 *
 * @param   gattMsg - pointer to the GATT message event structure
 *
 * output parameters
 *
 * @param   None
 *
 * @return  none
 */
static void rreq_handleReadRsp(gattMsgEvent_t *gattMsg)
{
    // Register to RAS server characteristics
    if(gattMsg != NULL &&
       gattMsg->hdr.status == SUCCESS &&
       gattMsg->connHandle < RREQ_MAX_CONN &&
       gRREQControlBlock.connInfo[gattMsg->connHandle].procedureAttr.procedureState == RREQ_STATE_READ_CHAR_FEATURE)
    {
        // Set state back to INIT
        gRREQControlBlock.connInfo[gattMsg->connHandle].procedureAttr.procedureState = RREQ_STATE_INIT;

        if (gRREQControlBlock.connInfo[gattMsg->connHandle].featureCharValue == 0)
        {
            // Save RAS feature characteristic value
            memcpy(&gRREQControlBlock.connInfo[gattMsg->connHandle].featureCharValue, gattMsg->msg.readRsp.pValue, sizeof(uint32_t));
        }

        // Set the mode depends on the application preference and the server features
        if ((gRREQControlBlock.connInfo[gattMsg->connHandle].featureCharValue & RAS_FEATURES_REAL_TIME) &&
            gRREQControlBlock.connInfo[gattMsg->connHandle].preferredMode == RREQ_MODE_REAL_TIME)
        {
            gRREQControlBlock.connInfo[gattMsg->connHandle].enableMode = RREQ_MODE_REAL_TIME;
        }
        else
        {
            // Whether the application preferred to use On-Demand, or the server doesn't
            // support Real-Time - use On-Demand
            gRREQControlBlock.connInfo[gattMsg->connHandle].enableMode = RREQ_MODE_ON_DEMAND;
        }

        // Register to the server characteristics
        rreq_registerConfigChars(gattMsg->connHandle);
    }
}

/**
 * @brief Checks if a specific registration (notification or indication) is enabled.
 *
 * This function determines whether a required subscription (notification or indication)
 * is registered/enabled based on the provided subscription bitmap and the specific bit
 * representing the subscription type.
 *
 * @param subNeeded         The required subscription type (notification, indication, or disable).
 * @param subscribeBitMap   The current bitmap representing enabled subscriptions.
 * @param subscribeBit      The bitmask corresponding to the subscription type to check.
 *
 * @return true  If the required subscription is registered or not needed.
 * @return false If the required subscription is needed but not registered.
 */
static bool rreq_checkRegistration(RREQConfigSubType_e subNeeded, uint8_t subscribeBitMap, RREQ_SubscribeBitMap_e subscribeBit)
{
    bool registered = true;

    if (subNeeded != RREQ_DISABLE_NOTIFY_INDICATE &&
        (subscribeBitMap & subscribeBit) == 0)
    {
        registered = false;
    }

    return registered;
}

/*********************************************************************
 * @fn      rreq_registerConfigChars
 *
 * @brief Registers characteristics for the RREQ,
 * using the config set by @ref RREQ_Start API.
 * This function is responsible for registering the characteristics
 * that the RREQ will use to communicate with the server.
 * It ensures that the necessary characteristics
 * are properly initialized and made available for use.
 * The characteristic that will be registered depends on the mode
 * that has been chosen - @ref RREQ_MODE_ON_DEMAND or @ref RREQ_MODE_REAL_TIME
 *
 * @param connHandle The connection handle for the RREQ.
 *
 * @return INVALIDPARAMETER - if the connection handle is invalid
 * @return bleIncorrectMode - if the client is not registered to
 *                            either On-Demand or Real-Time characteristics.
 * @return SUCCESS - otherwise.
 */
static bStatus_t rreq_registerConfigChars(uint16_t connHandle)
{
    bStatus_t status = SUCCESS;

    if (connHandle >= RREQ_MAX_CONN)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        if (gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_ON_DEMAND)
        {
            status = rreq_registerOnDemandConfigChars(connHandle);
        }
        else if(gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_REAL_TIME)
        {
            status = rreq_registerRealTimeConfigChars(connHandle);
        }
        else
        {
            status = bleIncorrectMode;
        }
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_registerOnDemandConfigChars
 *
 * @brief Registers On-Demand characteristics for the RREQ,
 * using the config set by @ref RREQ_Start API.
 * This function is responsible for registering the characteristics
 * that the RREQ will use to communicate with the server.
 * It ensures that the necessary characteristics
 * are properly initialized and made available for use.
 *
 * @param connHandle The connection handle for the RREQ.
 *
 * @return INVALIDPARAMETER - if the connection handle is invalid or
 *                            the global configuration is NULL.
 * @return SUCCESS - otherwise.
 */
static bStatus_t rreq_registerOnDemandConfigChars(uint16_t connHandle)
{
    bStatus_t status = SUCCESS;

    // Check parameters and configuration
    if (connHandle >= RREQ_MAX_CONN ||
        gRREQControlBlock.config == NULL)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        // Register to the "control point" characteristic handle
        if (gRREQControlBlock.config->onDemandSubConfig.controlPointSubType != RREQ_DISABLE_NOTIFY_INDICATE &&
            (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_CONTROL_POINT_BIT) == 0)
        {
            status = rreq_configureCharRegistration(
                        connHandle,
                        RAS_CONTROL_POINT_UUID,
                        gRREQControlBlock.config->onDemandSubConfig.controlPointSubType
            );
        }

        // Register to the "data ready" characteristic handle
        else if (gRREQControlBlock.config->onDemandSubConfig.dataReadySubType != RREQ_DISABLE_NOTIFY_INDICATE &&
                 (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_DATA_READY_BIT) == 0)
        {
            status = rreq_configureCharRegistration(
                        connHandle,
                        RAS_DATA_READY_UUID,
                        gRREQControlBlock.config->onDemandSubConfig.dataReadySubType
            );
        }

        // Register to the "data overwritten" characteristic handle
        else if (gRREQControlBlock.config->onDemandSubConfig.overwrittenSubType != RREQ_DISABLE_NOTIFY_INDICATE &&
                 (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_OVERWRITTEN_BIT) == 0)
        {
            status = rreq_configureCharRegistration(
                        connHandle,
                        RAS_DATA_OVERWRITTEN_UUID,
                        gRREQControlBlock.config->onDemandSubConfig.overwrittenSubType
            );
        }

        // Register to the "on-demand" characteristic handle
        else if (gRREQControlBlock.config->onDemandSubConfig.onDemandSubType != RREQ_DISABLE_NOTIFY_INDICATE &&
                 (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_ON_DEMAND_BIT) == 0)
        {
            status = rreq_configureCharRegistration(
                        connHandle,
                        RAS_ON_DEMAND_UUID,
                        gRREQControlBlock.config->onDemandSubConfig.onDemandSubType
            );
        }

        if(rreq_checkRegistration(gRREQControlBlock.config->onDemandSubConfig.controlPointSubType, gRREQControlBlock.connInfo[connHandle].subscribeBitMap, RREQ_CONTROL_POINT_BIT) &&
        rreq_checkRegistration(gRREQControlBlock.config->onDemandSubConfig.dataReadySubType, gRREQControlBlock.connInfo[connHandle].subscribeBitMap, RREQ_DATA_READY_BIT) &&
        rreq_checkRegistration(gRREQControlBlock.config->onDemandSubConfig.overwrittenSubType, gRREQControlBlock.connInfo[connHandle].subscribeBitMap, RREQ_OVERWRITTEN_BIT) &&
        rreq_checkRegistration(gRREQControlBlock.config->onDemandSubConfig.onDemandSubType, gRREQControlBlock.connInfo[connHandle].subscribeBitMap, RREQ_ON_DEMAND_BIT))
        {
            // Done reading all relevant characteristics, set state to Idle
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
        }
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_registerRealTimeConfigChars
 *
 * @brief Registers Real-Time characteristics for the RREQ,
 * using the config set by @ref RREQ_Start API.
 * This function is responsible for registering the characteristics
 * that the RREQ will use to communicate with the server.
 * It ensures that the necessary characteristics
 * are properly initialized and made available for use.
 *
 * @param connHandle The connection handle for the RREQ.
 *
 * @return INVALIDPARAMETER - if the connection handle is invalid or
 *                            the global configuration is NULL.
 * @return SUCCESS - otherwise.
 */
static bStatus_t rreq_registerRealTimeConfigChars(uint16_t connHandle)
{
    bStatus_t status = SUCCESS;

    // Check parameters and configuration
    if (connHandle >= RREQ_MAX_CONN ||
        gRREQControlBlock.config == NULL)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        // Register to the "Real-Time" characteristic handle
        if(gRREQControlBlock.config->realTimeSubConfig.realTimeSubType != RREQ_DISABLE_NOTIFY_INDICATE &&
           (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_REAL_TIME_BIT) == 0)
        {
            status = rreq_configureCharRegistration(
                        connHandle,
                        RAS_REAL_TIME_UUID,
                        gRREQControlBlock.config->realTimeSubConfig.realTimeSubType
            );
        }

        if(rreq_checkRegistration(gRREQControlBlock.config->realTimeSubConfig.realTimeSubType, gRREQControlBlock.connInfo[connHandle].subscribeBitMap, RREQ_REAL_TIME_BIT))
        {
            // Done reading all relevant characteristics, set state to Idle
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
        }
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_configureCharRegistration
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
 * @return SUCCESS          - if the configuration was successful.
 * @return INVALIDPARAMETER - if the connection handle is invalid, the characteristic
 *                            UUID is not supported, or if trying to register to both
 *                            Real-Time and On-Demand characteristics.
 *
 * @note Does not accept RAS_FEATURE_UUID for configuration.
 */
static bStatus_t rreq_configureCharRegistration(uint16_t connHandle, uint16_t charUUID, RREQConfigSubType_e mode)
{
    bStatus_t status = INVALIDPARAMETER;

    if (connHandle >= RREQ_MAX_CONN)
    {
        return status;
    }

    switch(mode)
    {
        case RREQ_DISABLE_NOTIFY_INDICATE:
        {
            status = rreq_unregisterCharacteristic(connHandle, charUUID);

            // Update the enable mode if both On-Demand and Real-Time characteristics are unregistered
            if (status == SUCCESS &&
                (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_UUID_TO_CHAR_SUBSCRIBE_BIT(RAS_ON_DEMAND_UUID)) == 0 &&
                (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_UUID_TO_CHAR_SUBSCRIBE_BIT(RAS_REAL_TIME_UUID)) == 0)
            {
                // If both On-Demand and Real-Time characteristics are unregistered,
                // set the enable mode and state to Idle
                gRREQControlBlock.connInfo[connHandle].enableMode = RREQ_MODE_NONE;
                gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
            }

            break;
        }
        case RREQ_PREFER_NOTIFY:
        case RREQ_INDICATE:
        {
            // Check if trying to register to both Real-Time and On-Demand characteristics
            if ((charUUID == RAS_REAL_TIME_UUID &&
                 (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_UUID_TO_CHAR_SUBSCRIBE_BIT(RAS_ON_DEMAND_UUID)) != 0) ||
                (charUUID == RAS_ON_DEMAND_UUID &&
                 (gRREQControlBlock.connInfo[connHandle].subscribeBitMap & RREQ_UUID_TO_CHAR_SUBSCRIBE_BIT(RAS_REAL_TIME_UUID)) != 0))
            {
                status = INVALIDPARAMETER;
            }
            else
            {
                status = rreq_registerCharacteristic(connHandle, charUUID, mode);
            }

            // Set the enable mode if there was a successful mode registration
            if (status == SUCCESS)
            {
                if (charUUID == RAS_REAL_TIME_UUID)
                {
                    gRREQControlBlock.connInfo[connHandle].enableMode = RREQ_MODE_REAL_TIME;
                }
                else if (charUUID == RAS_ON_DEMAND_UUID)
                {
                    gRREQControlBlock.connInfo[connHandle].enableMode = RREQ_MODE_ON_DEMAND;
                }
            }

            break;
        }
        default:
        {
            break;
        }
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_registerCharacteristic
 *
 * @brief Registers a characteristic for the ranging request profile client.
 *
 * This function registers a characteristic with the specified UUID on the given connection handle,
 * configuring it according to the provided mode.
 *
 * @param connHandle   The connection handle identifying the BLE connection.
 * @param charUUID     The UUID of the characteristic to register.
 * @param mode         The configuration mode (of type RREQConfigSubType_e) for the characteristic.
 *
 * @return SUCCESS - if the configuration was successful.
 *         INVALIDPARAMETER - if the connection handle is invalid or the characteristic
 *                            UUID is not supported.
 *
 * @note Does not accept RAS_FEATURE_UUID for configuration.
 */
static bStatus_t rreq_registerCharacteristic(uint16_t connHandle, uint16_t charUUID, RREQConfigSubType_e mode)
{
    bStatus_t status = INVALIDPARAMETER;
    RREQConfigSubType_e charSubType;
    uint8_t subscribeBit;
    RREQ_CharInfo_t charInfo;

    if (connHandle < RREQ_MAX_CONN &&
        mode <= RREQ_INDICATE &&
        charUUID >= RAS_REAL_TIME_UUID &&
        charUUID <= RAS_DATA_OVERWRITTEN_UUID)
    {
        subscribeBit = RREQ_UUID_TO_CHAR_SUBSCRIBE_BIT(charUUID);
        charInfo = gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_UUID_TO_CHAR_INDEX(charUUID)];

        if ((gRREQControlBlock.connInfo[connHandle].subscribeBitMap & subscribeBit) == 0)
        {
            // Get the characteristic properties mode
            charSubType = rreq_getCharProperties(mode, charInfo.charProperties);
            // Enable notifications/indications for the characteristic
            status = rreq_enableNotification(connHandle, charSubType, charInfo.charHandle);

            if (status == SUCCESS)
            {
                gRREQControlBlock.connInfo[connHandle].subscribeBitMap |= subscribeBit;
            }
        }
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_unregisterCharacteristic
 *
 * @brief Unregisters a characteristic for a given connection handle and characteristic UUID.
 *
 * This function removes the registration of a characteristic, identified by its UUID,
 * from the specified BLE connection. After unregistration, notifications or indications
 * for this characteristic will no longer be received for the given connection.
 *
 * @param connHandle The connection handle identifying the BLE connection.
 * @param charUUID   The UUID of the characteristic to unregister.
 *
 * @return bStatus_t Returns status of the operation.
 *         - SUCCESS if the characteristic was unregistered successfully.
 *         - Otherwise, an appropriate error code.
 *
 * @note Does not accept RAS_FEATURE_UUID for unregistration.
 */
static bStatus_t rreq_unregisterCharacteristic(uint16_t connHandle, uint16_t charUUID)
{
    bStatus_t status = INVALIDPARAMETER;
    uint8_t subscribeBit;
    uint16_t charHandle;

    if (connHandle < RREQ_MAX_CONN &&
        charUUID >= RAS_REAL_TIME_UUID &&
        charUUID <= RAS_DATA_OVERWRITTEN_UUID)
    {
        subscribeBit = RREQ_UUID_TO_CHAR_SUBSCRIBE_BIT(charUUID);
        charHandle = gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_UUID_TO_CHAR_INDEX(charUUID)].charHandle;

        if ((gRREQControlBlock.connInfo[connHandle].subscribeBitMap & subscribeBit) != 0)
        {
            // Unregister peer notifications/indications
            status = rreq_enableNotification(connHandle, RREQ_DISABLE_NOTIFY_INDICATE, charHandle);

            if (status == SUCCESS)
            {
                gRREQControlBlock.connInfo[connHandle].subscribeBitMap &= (~subscribeBit);
            }
        }
    }


    return status;
}

/*********************************************************************
 * @fn      rreq_handleValueNoti
 *
 * @brief Handles the GATT "BLEAPPUTIL_ATT_HANDLE_VALUE_NOTI".
 * This function processes the response received from the GATT server
 * after getting "BLEAPPUTIL_ATT_HANDLE_VALUE_NOTI".
 * It is responsible for handling the data returned in the response
 * and performing any necessary actions based on the received information.
 *
 * @param gattMsg Pointer to the GATT message event structure containing
 *                the response data.
 */
static void rreq_handleValueNoti(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti)
{
    if (connHandle < RREQ_MAX_CONN &&
        handleValueNoti != NULL)
    {
        if (gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_ON_DEMAND)
        {
            rreq_handleOnDemandValueNoti(connHandle, handleValueNoti);
        }
        else if(gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_REAL_TIME)
        {
            rreq_handleRealTimeValueNoti(connHandle, handleValueNoti);
        }
        else
        {
            // Don't do anything
        }
    }
}

/*********************************************************************
 * @fn      rreq_handleOnDemandValueNoti
 *
 * @brief Handles on-demand value notification for the ranging profile client.
 *
 * This function processes incoming ATT Handle Value Notification messages
 * related to on-demand values for a specific connection handle. It is typically
 * invoked when a notification is received from the server, allowing the client
 * to react to updated ranging data or status.
 *
 * @param connHandle        The connection handle identifying the BLE connection.
 * @param handleValueNoti   Pointer to the ATT Handle Value Notification structure
 *                          containing the notification data.
 */
static void rreq_handleOnDemandValueNoti(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti)
{
    if (connHandle >= RREQ_MAX_CONN || handleValueNoti == NULL)
    {
        return;
    }

    // if get notification from "on-demand" characteristic handle
    if(handleValueNoti->handle == gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_ON_DEMAND_CHAR_INDEX].charHandle )
    {
        rreq_handleOnDemandSegmentReceived(connHandle, handleValueNoti);
    }

    // if get notification from "control point" characteristic handle
    else if(handleValueNoti->handle == gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_CONTROL_POINT_CHAR_INDEX].charHandle )
    {
        rreq_handleControlPointRsp(connHandle, handleValueNoti);
    }

    // if get notification from "data ready" characteristic handle
    else if(handleValueNoti->handle == gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_DATA_READY_CHAR_INDEX].charHandle )
    {
        // Get back to Idle
        gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;

        // check the notification length
        if(handleValueNoti->len >= 2)
        {
            // Get from the notification the ranging counter
            uint8_t *pData = handleValueNoti->pValue;
            uint16_t rangingCounter = BUILD_UINT16(pData[0], pData[1]);

            if((gRREQControlBlock.callbacks != NULL) && (gRREQControlBlock.callbacks->pDataReadyCallback != NULL))
            {
                // Call pDataReadyCallback function
                gRREQControlBlock.callbacks->pDataReadyCallback(connHandle, rangingCounter);
            }
        }
    }

    // if get notification from "data overwritten" characteristic handle
    else if((gRREQControlBlock.callbacks != NULL) &&
            (gRREQControlBlock.callbacks->pStatusCallback != NULL) &&
            (handleValueNoti->handle == gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_DATA_OVERWRITTEN_CHAR_INDEX].charHandle))
    {
        // send OverWritten status to App
        uint16_t rangingCounter = BUILD_UINT16(handleValueNoti->pValue[0], handleValueNoti->pValue[1]);
        gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_DATA_OVERWRITTEN, RANGING_COUNTER_LEN, (uint8_t*)&rangingCounter);
    }
}

/*********************************************************************
 * @fn      rreq_handleRealTimeValueNoti
 *
 * @brief Handles real-time value notification for the ranging profile client.
 *
 * This function processes incoming ATT Handle Value Notification messages
 * related to real-time values from the server. It is typically called when
 * a notification is received on the corresponding characteristic.
 *
 * @param connHandle        Connection handle identifying the BLE connection.
 * @param handleValueNoti   Pointer to the ATT Handle Value Notification structure
 *                          containing the notification data.
 */
static void rreq_handleRealTimeValueNoti(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti)
{
    if (connHandle >= RREQ_MAX_CONN || handleValueNoti == NULL)
    {
        return;
    }

    // if get notification from "Real-Time" characteristic handle
    if(handleValueNoti->handle == gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_REAL_TIME_CHAR_INDEX].charHandle )
    {
        rreq_handleRealTimeSegmentReceived(connHandle, handleValueNoti);
    }
}

/*********************************************************************
 * @fn    rreq_handleControlPointRsp
 *
 * @brief Handles the control point indications
 *
 * @param connHandle The connection handle of the ranging profile.
 * @param handleValueNoti Pointer to the handle value indication structure.
 *
 * @return None.
 */
void rreq_handleControlPointRsp(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti)
{
    if (connHandle >= RREQ_MAX_CONN || handleValueNoti == NULL)
    {
        return;
    }

    // Check if the notification length is valid
    if(handleValueNoti->len <= RAS_CP_RSP_MAX_LEN)
    {
        // Get the command from the notification
        uint8_t RspOpCode = handleValueNoti->pValue[0];

        // Handle the command based on its type
        switch(RspOpCode)
        {
            case RAS_CP_OPCODE_COMPLETE_DATA_RSP:
            {
                // RangingCounter received from CompleteData msg
                uint16_t CompleteDataRangingCounter = BUILD_UINT16(handleValueNoti->pValue[1], handleValueNoti->pValue[2]);

                // check that the received rangingCounter is the expected one.
                if (gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter == CompleteDataRangingCounter)
                {
                    // wait for the response code
                    gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_WAIT_FOR_CONTROL_POINT_RSP;

                    // Stop the timer
                    rreq_stopTimer(connHandle);

                    // Send Ack to the server anyway due to the fact that there is no support in Get Lost Segments
                    rreq_sendControlPointWriteCmd(connHandle, RAS_CP_OPCODE_ACK_RANGING_DATA, CompleteDataRangingCounter,
                                                RAS_CP_ACK_DATA_CMD_LEN);
                }
                break;
            }
            case RAS_CP_OPCODE_RSP_CODE:
            {
                // Notify App
                uint8_t rspValue = handleValueNoti->pValue[1];
                rreq_handleRspCode(connHandle, rspValue);
                break;
            }
            default:
                // Unknown command, do nothing or log an error
                break;
        }
    }
}

/*********************************************************************
 * @fn    rreq_procedureDone
 *
 * @brief This function handles the completion of the RREQ procedure.
 *        It checks if the data is complete using a bitmask,
 *        notifies the application about the completion status,
 *        and resets the procedure attributes.
 *
 * @param  None.
 *
 * @return None.
 *
 */
static void rreq_procedureDone(uint16_t connHandle)
{
    bStatus_t status = SUCCESS;
    RangingDBClient_procedureSegmentsReader_t segmentsReader;

    if (connHandle >= RREQ_MAX_CONN)
    {
        return;
    }

    // Notify APP ( Data Complete )
    if((gRREQControlBlock.callbacks != NULL) && (gRREQControlBlock.callbacks->pDataCompleteEventCallback != NULL))
    {
        // Check if the we received the last segment
        if (gRREQControlBlock.connInfo[connHandle].segmentMgr.lastSegmentFlag != TRUE)
        {
            status = RREQ_DATA_INVALID;
        }

        if(status == SUCCESS)
        {
            // Get the data from the database
            status = RangingDBClient_getData(connHandle, &segmentsReader);
        }

        // Call the data complete callback when the procedure is done in any case, to notify the end of the current procedure.
        gRREQControlBlock.callbacks->pDataCompleteEventCallback(connHandle,
                                                                gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter,
                                                                status,
                                                                segmentsReader);
    }
    else
    {
        // If there is no callback - clear the data from the database
        RangingDBClient_clearProcedure(connHandle);
    }

    // Reset segment counter
    clearSegmentMgr(connHandle);

    // Stop any active timer
    rreq_stopTimer(connHandle);

    // Reset Procedure Attributes
    gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter = 0xFFFF;
    gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
}
/*********************************************************************
 * @fn      rreq_handleRspCode
 *
 * @brief   Handles the response code received from the server.
 *          This function processes the response code and calls the
 *          appropriate callback function based on the response value.
 *
 * @param   connHandle - The connection handle for the RAS client.
 * @param   rspValue - The response value received from the server.
 *
 * @return  None.
 */
static void rreq_handleRspCode(uint16_t connHandle, uint8_t rspValue)
{
    // Check parameters and that the callback is set
    if (connHandle >= RREQ_MAX_CONN ||
        gRREQControlBlock.callbacks == NULL ||
        gRREQControlBlock.callbacks->pStatusCallback == NULL)
    {
        return;
    }

    // Handle the response code based on its value
    switch(rspValue)
    {
        case RAS_CP_RSP_CODE_VAL_SUCCESS:
        {
            if(gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState == RREQ_STATE_WAIT_FOR_ABORT)
            {
                // Procedure aborted successfully
                gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_ABORTED_SUCCESSFULLY, RANGING_COUNTER_LEN,
                                                                (uint8_t*)&gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter);
                gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;

                // Stop any active timer for this connection
                rreq_stopTimer(connHandle);
            }
            else if(gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState == RREQ_STATE_WAIT_FOR_CONTROL_POINT_RSP)
            {
                // handle the procedure completion
                rreq_procedureDone(connHandle);
            }
            break;
        }
        case RAS_CP_RSP_CODE_ABORT_UNSUCCESSFUL:
        {
            if(gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState == RREQ_STATE_WAIT_FOR_ABORT)
            {
                // Reset the procedure state to idle
                gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
            }
            gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_ABORTED_UNSUCCESSFULLY, RANGING_COUNTER_LEN,
                                                            (uint8_t*)&gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter);

            break;
        }
        case RAS_CP_RSP_CODE_SERVER_BUSY:
        {
            // Procedure not completed
            gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_SERVER_BUSY , RANGING_COUNTER_LEN,
                                                            (uint8_t*)&gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter);
            // Reset the procedure state to idle
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
            // Stop the timer
            rreq_stopTimer(connHandle);
            break;
        }
        case RAS_CP_RSP_CODE_NO_RECORDS_FOUND:
        {
            // No records found
            gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_NO_RECORDS, RANGING_COUNTER_LEN,
                                                            (uint8_t*)&gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter);
            // Reset the procedure state to idle
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
            // Stop the timer
            rreq_stopTimer(connHandle);
            break;
        }
        case RAS_CP_RSP_CODE_PROCEDURE_NOT_COMP:
        {
            gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_PROCEDURE_NOT_COMPLETED, RANGING_COUNTER_LEN,
                                                            (uint8_t*)&gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter);
            // Reset the procedure state to idle
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
            // Stop the timer
            rreq_stopTimer(connHandle);
            break;
        }
        default:
        {
            break;
        }
    }
}

/*********************************************************************
 * @fn      rreq_parseSegmentReceived
 *
 * @brief Handles the "on-demand" notification.
 *
 * @param connHandle - The connection handle for the RAS client.
 * @param handleValueNoti - Pointer to the handle value notification structure.
 *
 * @return None.
 */
static void rreq_parseSegmentReceived(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti)
{
    if (connHandle >= RREQ_MAX_CONN ||
        handleValueNoti == NULL)
    {
        return;
    }

    // split data to segment (1 byte) and the rest of the data
    uint8_t *pData = handleValueNoti->pValue;
    uint8_t segmentNum = RAS_LAST_6_BITS_LSB(pData[0]); // Extract the last 6 bits (LSB)
    uint8_t segmentFlag = RAS_FIRST_2_BITS_LSB(pData[0]); // Extract the first 2 bits (LSB)
    uint16_t dataLen = handleValueNoti->len - 1; // Exclude the segment byte

    // Check if the segment is the first one
    if( (segmentFlag & RAS_FIRST_SEGMENT_BIT_MASK) != 0 )
    {
        // Clear the procedure DB
        RangingDBClient_clearProcedure(connHandle);

        // Clear the segment manager
        memset(&gRREQControlBlock.connInfo[connHandle].segmentMgr, 0, sizeof(RREQSegmentsMGR_t));
    }

    // Check if the segment is the last one
    if( (segmentFlag & RAS_LAST_SEGMENT_BIT_MASK) != 0 )
    {
        gRREQControlBlock.connInfo[connHandle].segmentMgr.lastSegmentFlag = TRUE;
    }

    // Add the data to the procedure DB
    RangingDBClient_addData(connHandle, segmentNum, dataLen, &pData[1]);
}

/*********************************************************************
 * @fn      rreq_sendControlPointWriteCmd
 *
 * @brief Sends a control point write command to the RAS client.
 * This function is responsible for transmitting a write command to the
 * control point of the RAS client.
 *
 * @param connHandle The connection handle for the RAS client.
 * @param cmd The command to be sent to the control point.
 * @param rangingCounter The ranging counter value to be included in the command.
 * @param len The length of the command data to be sent.
 *
 * @return INVALIDPARAMETER: Connection handle is invalid or command length is out of range.
 * @return bleMemAllocError: Memory allocation error occurred.
 * @return A status generated by @ref GATT_WriteNoRsp otherwise
 */
static bStatus_t rreq_sendControlPointWriteCmd(uint16_t connHandle, uint8_t cmd, uint16_t rangingCounter, uint8_t len)
{
    bStatus_t status = SUCCESS;
    attWriteReq_t req;
    uint8 dataValue[RAS_CP_COMMANDS_MAX_LEN] = {0};

    // Check if the connHandle and command length are valid
    if (connHandle >= RREQ_MAX_CONN ||
        len < RAS_CP_COMMANDS_MIN_LEN ||
        len > RAS_CP_COMMANDS_MAX_LEN)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        // Set the command to be sent
        dataValue[0] = cmd;
        dataValue[1] = LO_UINT16(rangingCounter);
        dataValue[2] = HI_UINT16(rangingCounter);

        // Allocate buffer for the write request
        req.pValue = GATT_bm_alloc(connHandle, ATT_WRITE_REQ, len, NULL);

        // Send the write request for indications enable / disable
        if (req.pValue != NULL)
        {
            req.handle = gRREQControlBlock.connInfo[connHandle].charInfo[RREQ_CONTROL_POINT_CHAR_INDEX].charHandle;
            req.len = len;
            memcpy(req.pValue, dataValue, len);
            req.cmd = TRUE;
            req.sig = FALSE;

            // Send the write request
            status = GATT_WriteNoRsp(connHandle, &req);

            // If the write request failed, free the buffer
            if ( status != SUCCESS )
            {
                GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
            }
        }
        else
        {
            status = bleMemAllocError;
        }
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_handleFindByTypeValueRsp
 *
 * @brief Handles the GATT "BLEAPPUTIL_ATT_FIND_BY_TYPE_VALUE_RSP".
 * This function processes the response received from the GATT server
 * after getting "BLEAPPUTIL_ATT_FIND_BY_TYPE_VALUE_RSP".
 * It is responsible for handling the data returned in the response
 * and performing any necessary actions based on the received information.
 *
 * @param gattMsg Pointer to the GATT message event structure containing
 *                the response data.
 */
static void rreq_handleFindByTypeValueRsp(gattMsgEvent_t *gattMsg)
{
    // Check parameters and procedure state
    if ( (gattMsg == NULL) || (gRREQControlBlock.connInfo[gattMsg->connHandle].procedureAttr.procedureState != RREQ_STATE_DISCOVER_PRIM_SERVICE) )
    {
        return;
    }

    if( gattMsg->hdr.status == SUCCESS )
    {
        // Save the start and end handles of the RAS service
        gRREQControlBlock.connInfo[gattMsg->connHandle].startHandle = ATT_ATTR_HANDLE(gattMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
        gRREQControlBlock.connInfo[gattMsg->connHandle].endHandle = ATT_GRP_END_HANDLE(gattMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
    }

    else if( gattMsg->hdr.status == bleProcedureComplete )
    {
        // Set state back to INIT
        gRREQControlBlock.connInfo[gattMsg->connHandle].procedureAttr.procedureState = RREQ_STATE_INIT;

        // Find all characteristics within the Car Access service
        rreq_discoverAllChars(gattMsg->connHandle, gRREQControlBlock.connInfo[gattMsg->connHandle].startHandle, gRREQControlBlock.connInfo[gattMsg->connHandle].endHandle);
    }
}

/*********************************************************************
 * @fn      rreq_handleReadByTypeRsp
 *
 * @brief Handles the GATT "BLEAPPUTIL_ATT_READ_BY_TYPE_RSP".
 * This function processes the response received from the GATT server
 * after getting "BLEAPPUTIL_ATT_READ_BY_TYPE_RSP".
 * It is responsible for handling the data returned in the response
 * and performing any necessary actions based on the received information.
 *
 * @param gattMsg Pointer to the GATT message event structure containing
 *                the response data.
 */
static void rreq_handleReadByTypeRsp(gattMsgEvent_t *gattMsg)
{
    attReadByTypeRsp_t att = gattMsg->msg.readByTypeRsp;

    if (gattMsg == NULL)
    {
        return;
    }

    if ( ( gattMsg->hdr.status == SUCCESS ) && ( att.numPairs > 0 ) )
    {
        for (uint8_t i = 0; i < att.numPairs; i++)
        {
            // The format of the data in att.pDataList for each characteristic is:
            // Properties handle: 2 bytes
            // Properties value: 1 byte
            // Char value handle: 2 bytes
            // Char value UUID: 2 bytes
            // Since the handle and value of the properties are not used in this case,
            // they are skipped for each characteristic
            uint16_t charUUID = 0;
            uint8_t customUUID[ATT_BT_UUID_SIZE];
            uint8_t index = i * att.len;

            // characteristic properties
            uint8_t CharPro = att.pDataList[index + RREQ_APP_CHAR_PRO_HANDLE_INDEX];
            // ATT handle
            uint16_t currAttHandle = BUILD_UINT16(att.pDataList[index + RREQ_APP_CHAR_VALUE_HANDLE_INDEX],
                                                  att.pDataList[index + RREQ_APP_CHAR_VALUE_HANDLE_INDEX + 1]);

            // Copy the full 16-bit custom UUID
            memcpy(customUUID, &att.pDataList[index + RREQ_APP_CHAR_UUID_HANDLE_INDEX], ATT_BT_UUID_SIZE);

            // Build the 16-bit UUID of the characteristic
            charUUID = BUILD_UINT16(customUUID[RREQ_APP_LOW_UUID_INDEX],
                                    customUUID[RREQ_APP_HIGH_UUID_INDEX]);

            if (charUUID >= RAS_FEATURE_UUID && charUUID <= RAS_DATA_OVERWRITTEN_UUID)
            {
                gRREQControlBlock.connInfo[gattMsg->connHandle].charInfo[RREQ_UUID_TO_CHAR_INDEX(charUUID)].charHandle = currAttHandle;
                gRREQControlBlock.connInfo[gattMsg->connHandle].charInfo[RREQ_UUID_TO_CHAR_INDEX(charUUID)].charProperties = CharPro;
            }
        }
    }
    else if( gattMsg->hdr.status == bleProcedureComplete )
    {
        // discover all characteristic Descriptors
        rreq_discoverAllCharDescriptors(gattMsg->connHandle);
    }
}

/*********************************************************************
 * @fn      rreq_discoverAllCharDescriptors
 *
 * @brief Discovers all characteristic descriptors for the RAS client.
 * This function is responsible for discovering all characteristic
 * descriptors associated with the RAS client.
 * It ensures that the necessary descriptors are properly initialized
 * and made available for use.
 *
 * @param connHandle The connection handle for the RAS client.
 * @return None.
 */
static void rreq_discoverAllCharDescriptors(uint16_t connHandle)
{
    bStatus_t status = SUCCESS;

    if (connHandle >= RREQ_MAX_CONN)
    {
        return;
    }

    // Discover all characteristic descriptors
    status = GATT_DiscAllCharDescs(connHandle, gRREQControlBlock.connInfo[connHandle].startHandle, gRREQControlBlock.connInfo[connHandle].endHandle, BLEAppUtil_getSelfEntity());

    if (status != SUCCESS)
    {
        // Handle the error if needed
    }
}

/*********************************************************************
 * @fn      rreq_discoverPrimServ
 *
 * @brief   RAS client discover primary service by UUID,
 *          ATT_FIND_BY_TYPE_VALUE_RSP
 *
 * @param   connHandle - connection message was received on
 *
 * @return  INVALIDPARAMETER: Connection handle is invalid
 * @return  Status generated by @ref GATT_DiscPrimaryServiceByUUID
 */
static bStatus_t rreq_discoverPrimServ(uint16_t connHandle)
{
    bStatus_t status = SUCCESS;

    if (connHandle >= RREQ_MAX_CONN)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        // Discovery Car Access service
        GATT_BT_UUID(rasUUID, RANGING_SERVICE_UUID);
        status = GATT_DiscPrimaryServiceByUUID(connHandle, rasUUID, ATT_BT_UUID_SIZE, BLEAppUtil_getSelfEntity());
    }

    if (status == SUCCESS)
    {
        // Set state to discover primary service
        gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_DISCOVER_PRIM_SERVICE;
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_getCharProperties
 *
 * @brief Retrieves the characteristic properties for a given connection handle and mode.
 * This function is used to obtain the characteristic properties associated with a specific
 * connection handle and request mode. It processes the provided properties and returns
 * the corresponding mode type.
 *
 * @param mode The request mode type to be used for retrieving the properties.
 * @param properties The characteristic properties to be processed.
 *
 * @return The mode type corresponding to the processed properties.
 */
static RREQConfigSubType_e rreq_getCharProperties(RREQConfigSubType_e mode, uint8_t properties)
{
    // set default value to indicatation
    RREQConfigSubType_e charPro = RREQ_INDICATE;

    // check if the characteristic supports notification and the desired mode is notification
    if( ((properties & RAS_NOTIFICATION_PRO_MASK) != 0) && (mode == RREQ_PREFER_NOTIFY) )
    {
        charPro = RREQ_PREFER_NOTIFY;
    }
    return charPro;
}

/*********************************************************************
 * @fn      rreq_enableNotification
 *
 * @brief Enables notifications for a specific attribute on a remote device.
 * This function enables notifications for a given attribute handle on a remote device
 * over a specified connection. It allows the application to receive updates from the
 * remote device when the value of the attribute changes.
 *
 * @param connHandle The connection handle identifying the link to the remote device.
 * @param mode The request mode type, specifying how the notification request should be handled.
 * @param attHandle The attribute handle for which notifications are to be enabled.
 *
 * @return INVALIDPARAMETER: The provided connection handle or attribute handle is invalid.
 * @return bleMemAllocError: Memory allocation error occurred.
 * @return A status generated by @ref GATT_WriteCharValue otherwise
 */
static bStatus_t rreq_enableNotification(uint16_t connHandle, RREQConfigSubType_e mode, uint16_t attHandle)
{
    bStatus_t status = SUCCESS;
    attWriteReq_t req;
    // Set the default value to disable notification
    uint8_t dataValue[RREQ_APP_CCCD_VALUE_LEN] = {0};

    if (connHandle >= RREQ_MAX_CONN ||
        attHandle == 0)
    {
        status = INVALIDPARAMETER;
    }

    if (status == SUCCESS)
    {
        if (mode == RREQ_PREFER_NOTIFY)
        {
            // Set the value to enable notification
            dataValue[0] = LO_UINT16(GATT_CLIENT_CFG_NOTIFY);
        }
        else if(mode == RREQ_INDICATE)
        {
            // Set the value to enable indication
            dataValue[0] = LO_UINT16(GATT_CLIENT_CFG_INDICATE);
        }
        else // mode == RREQ_DISABLE_NOTIFY_INDICATE
        {
            // Set the value to disable notification/indication
            dataValue[0] = 0;
        }

        // Allocate buffer for the write request
        req.pValue = GATT_bm_alloc(connHandle, ATT_WRITE_REQ, RREQ_APP_CCCD_VALUE_LEN, NULL);

        // Send the write request for Notifications enable/diable
        if (req.pValue != NULL)
        {
            req.handle = attHandle + RREQ_APP_CCCD_OFFSET;
            req.len = RREQ_APP_CCCD_VALUE_LEN;
            memcpy(req.pValue, dataValue, RREQ_APP_CCCD_VALUE_LEN);
            req.cmd = FALSE;
            req.sig = FALSE;

            status = GATT_WriteCharValue(connHandle, &req, BLEAppUtil_getSelfEntity());

            // If the write request failed, free the buffer
            if ( status != SUCCESS )
            {
                GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
            }
        }
        else
        {
            status = bleMemAllocError;
        }
    }

    return status;
}

/*********************************************************************
 * @fn      rreq_clearData
 *
 * @brief Clears all data associated with the specified connection handle.
 * This function is responsible for clearing all data related to a specific
 * connection handle. It resets the relevant variables and structures to
 * ensure that no stale data remains.
 *
 * @param connHandle The connection handle for which data should be cleared.
 */
static void rreq_clearData(uint16_t connHandle)
{
    if (connHandle >= RREQ_MAX_CONN)
    {
        return;
    }

    // Clear the start/end handles
    gRREQControlBlock.connInfo[connHandle].startHandle = 0;
    gRREQControlBlock.connInfo[connHandle].endHandle = 0;

    // Clear the characteristic info
    memset(gRREQControlBlock.connInfo[connHandle].charInfo, 0, sizeof(gRREQControlBlock.connInfo[connHandle].charInfo));

    gRREQControlBlock.connInfo[connHandle].preferredMode = RREQ_MODE_ON_DEMAND;
    gRREQControlBlock.connInfo[connHandle].enableMode = RREQ_MODE_NONE;
    gRREQControlBlock.connInfo[connHandle].subscribeBitMap = 0;
    gRREQControlBlock.connInfo[connHandle].featureCharValue = 0;
    gRREQControlBlock.connInfo[connHandle].currentProcedureCounter = RREQ_INVALID_CS_PROCEDURE_COUNTER;
    gRREQControlBlock.connInfo[connHandle].timeoutHandle = BLEAPPUTIL_TIMER_INVALID_HANDLE;

    // Clear procedure attribute
    gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;
    gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter = 0xFFFF;

    // Clear the segment manager
    clearSegmentMgr(connHandle);
}

static void clearSegmentMgr(uint16_t connHandle)
{
    memset(&gRREQControlBlock.connInfo[connHandle].segmentMgr, 0, sizeof(RREQSegmentsMGR_t));
}

/*********************************************************************
 * @fn      rreq_handleOnDemandSegmentReceived
 *
 * @brief   Handles the reception of an on-demand segment notification from the server.
 * This function processes a received ATT Handle Value Notification containing an on-demand
 * segment from the server for a specific connection handle. It is typically called when
 * a notification is received on the characteristic associated with on-demand data segments.
 *
 * @param connHandle The connection handle for the RAS server.
 * @param handleValueNoti Pointer to the handle value notification structure.
 *
 * @return None.
 */
static void rreq_handleOnDemandSegmentReceived(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti)
{
    // Check parameters and configuration
    if (connHandle >= RREQ_MAX_CONN ||
        handleValueNoti == NULL ||
        gRREQControlBlock.config == NULL ||
        gRREQControlBlock.connInfo[connHandle].enableMode != RREQ_MODE_ON_DEMAND)
    {
        return;
    }

    switch (gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState)
    {
        case RREQ_STATE_WAIT_FOR_NEXT_SEGMENT:
        {
            // Start the timer for timeout
            rreq_startTimer(gRREQControlBlock.config->timeoutConfig.timeOutNextSegment, connHandle);
            // Handle the segment received
            rreq_parseSegmentReceived(connHandle, handleValueNoti);
            break;
        }

        default:
            // Unknown state, do nothing
            break;
    }
}

/*********************************************************************
 * @fn      rreq_handleRealTimeSegmentReceived
 *
 * @brief Handles the reception of a real-time segment notification from the server.
 *
 * This function processes a received ATT Handle Value Notification containing a real-time
 * segment from the server for a specific connection handle. It is typically called when
 * a notification is received on the characteristic associated with real-time data segments.
 *
 * @param connHandle         The connection handle identifying the BLE connection.
 * @param handleValueNoti    Pointer to the ATT Handle Value Notification structure containing
 *                           the received data segment.
 */
static void rreq_handleRealTimeSegmentReceived(uint16_t connHandle, attHandleValueNoti_t *handleValueNoti)
{
    bStatus_t status = SUCCESS;
    uint8_t segmentFlag;
    bool isLastSegment = FALSE;

    // Check parameters and configuration
    if (connHandle >= RREQ_MAX_CONN      ||
        handleValueNoti == NULL          ||
        handleValueNoti->len == 0        ||
        gRREQControlBlock.config == NULL ||
        gRREQControlBlock.connInfo[connHandle].enableMode != RREQ_MODE_REAL_TIME)
    {
        return;
    }

    // Get segment numbers
    segmentFlag = RAS_FIRST_2_BITS_LSB(handleValueNoti->pValue[0]); // Extract the first 2 bits (LSB)

    switch (gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState)
    {
        case RREQ_STATE_WAIT_FOR_FIRST_SEGMENT:
        {
            if ( (segmentFlag & RAS_FIRST_SEGMENT_BIT_MASK) != 0)
            {
                // Update state and the current ranging counter
                gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_WAIT_FOR_NEXT_SEGMENT;
                gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter = gRREQControlBlock.connInfo[connHandle].currentProcedureCounter;

                // Stop the first segment timer
                rreq_stopTimer(connHandle);

                // If this is the last segment
                if( (segmentFlag & RAS_LAST_SEGMENT_BIT_MASK) != 0 )
                {
                    // Mark as last segment
                    isLastSegment = TRUE;
                }
                else
                {
                    // Start the timer for the next segment only when this is not the last segment
                    rreq_startTimer(gRREQControlBlock.config->timeoutConfig.timeOutNextSegment, connHandle);
                }

                // Handle the segment received
                rreq_parseSegmentReceived(connHandle, handleValueNoti);
            }
            else
            {
                // Lost first segment, consider as failure
                status = FAILURE;
                rreq_procedureDone(connHandle);
            }

            break;
        }
        case RREQ_STATE_WAIT_FOR_NEXT_SEGMENT:
        {
            // If this is the last segment
            if( (segmentFlag & RAS_LAST_SEGMENT_BIT_MASK) != 0 )
            {
                isLastSegment = TRUE;

                // stop the timer
                rreq_stopTimer(connHandle);
            }
            else
            {
                // This is not the first segment - start the timer for the next segment one
                rreq_startTimer(gRREQControlBlock.config->timeoutConfig.timeOutNextSegment, connHandle);
            }

            // Handle the segment received
            rreq_parseSegmentReceived(connHandle, handleValueNoti);

            break;
        }

        default:
        {
            // Unknown state, do nothing
            status = FAILURE;
            break;
        }
    }

    // If received the last segment - send the data to the application if possible
    if (status == SUCCESS && isLastSegment == true)
    {
        rreq_procedureDone(connHandle);
    }
}

/*********************************************************************
 * @fn      rreq_startTimer
 *
 * @brief   Start the timer for the RREQ procedure.
 *
 * @param   timeout - The new timeout value in milliseconds.
 * @param   connHandle - The connection handle for which the timer is started,
 *                       passed as a parameter to the timer callback function.
 *
 * @return  None
 */
static void rreq_startTimer( uint32_t timeout, uint16_t connHandle )
{
    if (connHandle >= RREQ_MAX_CONN)
    {
        return;
    }

    if (gRREQControlBlock.connInfo[connHandle].timeoutHandle != BLEAPPUTIL_TIMER_INVALID_HANDLE)
    {
        // Stop the timer if it is already running
        BLEAppUtil_abortTimer(gRREQControlBlock.connInfo[connHandle].timeoutHandle);
    }

    // Start the timer
    gRREQControlBlock.connInfo[connHandle].timeoutHandle =
                    BLEAppUtil_startTimer(rreq_timerCB, timeout, false, (void*) ((uint32_t) connHandle));
}

/*********************************************************************
 * @fn      rreq_stopTimer
 *
 * @brief   Stop the timer for the RREQ procedure.
 *
 * @param   connHandle - The connection handle for which the timer is stopped.
 *
 * @return  None
 */
static void rreq_stopTimer( uint16_t connHandle )
{
    if (connHandle >= RREQ_MAX_CONN)
    {
        return;
    }

    if (gRREQControlBlock.connInfo[connHandle].timeoutHandle != BLEAPPUTIL_TIMER_INVALID_HANDLE)
    {
        // Abort the active timer and reset the timer handle
        BLEAppUtil_abortTimer(gRREQControlBlock.connInfo[connHandle].timeoutHandle);
        gRREQControlBlock.connInfo[connHandle].timeoutHandle = BLEAPPUTIL_TIMER_INVALID_HANDLE;
    }
}

/*********************************************************************
 * @fn      rreq_timerCB
 *
 * @brief   Timer callback function for the RREQ enable process.
 *          This function is called when the timer expires.
 *          It checks the reason for the timer expiration and
 *          performs the necessary actions.
 *
 * input parameters
 *
 * @param   timerHandle - The handle of the timer that expired.
 * @param   reason - The reason for the timer expiration.
 * @param   pData - Pointer to the data associated with the timer.
 *
 * output parameters
 *
 * @param   None
 *
 * @return None
 */
static void rreq_timerCB(BLEAppUtil_timerHandle timerHandle, BLEAppUtil_timerTermReason_e reason, void *pData)
{
  if (reason == BLEAPPUTIL_TIMER_TIMEOUT)
  {
    uint16_t connHandle = (uint16_t) (((uint32_t) pData) & 0xFFFF);

    switch (gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState)
    {
        case RREQ_STATE_WAIT_FOR_FIRST_SEGMENT:
        {
            if((gRREQControlBlock.callbacks != NULL) &&
               (gRREQControlBlock.callbacks->pStatusCallback != NULL) &&
               (gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_REAL_TIME))
            {
                // Unregister Real-Time characteristic
                rreq_configureCharRegistration(connHandle, RAS_REAL_TIME_UUID, RREQ_DISABLE_NOTIFY_INDICATE);

                // Notify App of timeout event, grab procedure counter from the current counter of this connection handle
                gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_TIMEOUT , RANGING_COUNTER_LEN, (uint8_t*)&gRREQControlBlock.connInfo[connHandle].currentProcedureCounter);
            }

            // Reset the procedure state
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;

            break;
        }
        case RREQ_STATE_WAIT_FOR_NEXT_SEGMENT:
        {
            if((gRREQControlBlock.callbacks != NULL) &&
               (gRREQControlBlock.callbacks->pStatusCallback != NULL) &&
               (gRREQControlBlock.connInfo[connHandle].enableMode != RREQ_MODE_NONE))
            {
                if (gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_ON_DEMAND)
                {
                    RREQ_Abort(connHandle);
                }
                else if (gRREQControlBlock.connInfo[connHandle].enableMode == RREQ_MODE_REAL_TIME)
                {
                    // Unregister Real-Time characteristic
                    rreq_configureCharRegistration(connHandle, RAS_REAL_TIME_UUID, RREQ_DISABLE_NOTIFY_INDICATE);
                }

                // Notify App of timeout event
                gRREQControlBlock.callbacks->pStatusCallback(connHandle, RREQ_TIMEOUT , RANGING_COUNTER_LEN,
                                                             (uint8_t*)&gRREQControlBlock.connInfo[connHandle].procedureAttr.rangingCounter);
            }

            // Reset the procedure state
            gRREQControlBlock.connInfo[connHandle].procedureAttr.procedureState = RREQ_STATE_IDLE;

            break;
        }
        default:

            // Unknown state, do nothing
            break;
    }

    // Reset the timer handle
    gRREQControlBlock.connInfo[connHandle].timeoutHandle = BLEAPPUTIL_TIMER_INVALID_HANDLE;
  }
}

#endif // RANGING_CLIENT
