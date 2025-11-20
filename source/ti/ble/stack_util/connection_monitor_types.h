/******************************************************************************

 @file  connection_monitor_types.h

 @brief This file contains the Connection Monitor types and data structers.

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

#ifndef CONNECTION_MONITOR_TYPES_H
#define CONNECTION_MONITOR_TYPES_H

/*******************************************************************************
 * INCLUDES
 */

#include "ti/ble/stack_util/bcomdef.h"

/*******************************************************************************
 * MACROS
 */

/*******************************************************************************
 * CONSTANTS
 */
#define CM_DEFAULT_MAX_FAILED_SYNC_TRIES   6

/*******************************************************************************
 * TYPEDEFS
 */

/** 
* @brief Connection Monitor error codes
*/
typedef enum
{
  CM_SUCCESS                   = 0x00, //!< Operation successful
  CM_FAILURE                   = 0x01, //!< Operation failed
  CM_INCORRECT_MODE            = 0x02, //!< Incorrect mode for operation
  CM_NOT_CONNECTED             = 0x03, //!< Device not connected
  CM_ALREADY_REQUESTED         = 0x04, //!< Operation already requested
  CM_INVALID_PARAMS            = 0x05, //!< Invalid parameters provided
  CM_NO_RESOURCE               = 0x06, //!< No resources available for operation
  CM_TIMEOUT                   = 0x07, //!< Operation timed out
  CM_CONNECTION_LIMIT_EXCEEDED = 0x08, //!< Connection limit exceeded
  CM_UNSUPPORTED               = 0x09, //!< Operation not supported
} cmErrorCodes_e;

/** 
* @brief Connection Monitor connection update event Callback
*/
typedef enum
{
  CM_PHY_UPDATE_EVT      = 0x00,  //!< Phy update event @ref cmPhyUpdateEvt_t
  CM_CHAN_MAP_UPDATE_EVT = 0x01,  //!< Channel map update event @ref cmChanMapUpdateEvt_t
  CM_PARAM_UPDATE_EVT    = 0x02,  //!< Parameter update event @ref cmParamUpdateEvt_t
  CM_INVALID_UPDATE      = 0xFF,  //!< Invalid update event
} cmConnUpdateEvtType_e;

/** 
* @brief Connection Monitor status event Callback
*/
typedef enum
{
  CM_TRACKING_START_EVT  = 0x00,  //!< Start monitor event @ref cmStartEvt_t
  CM_TRACKING_STOP_EVT   = 0x01,  //!< Stop monitor event @ref cmStopEvt_t
} cmStatusEvtType_e;

/** 
* @brief Connection Monitor stop reason
*/
typedef enum
{
  CM_SUPERVISION_TIMEOUT       = 0x00,
  CM_USER_TERM                 = 0x01,
  CM_SYNC_FAILED               = 0x02,
  CM_BAD_UPDATE_EVENT          = 0x03,
} cmStopReason_e;

/** 
* @brief Connection Monitor Status event
*/
typedef struct
{
  cmStatusEvtType_e   evtType;  //!< Event type @ref cmStatusEvtType_e
  uint8_t*            pEvtData; //!< Pointer to the event data, from type @ref cmStartEvt_t or @ref cmStopEvt_t
} cmStatusEvt_t;

/** 
* @brief Connection Monitor PHY Update event structure
*/
typedef struct
{
  uint8_t  phy;              //!< The new PHY of the monitored connection
} cmPhyUpdateEvt_t;

/** 
* @brief Connection Monitor Channel Map Update event structure
*/
typedef struct
{
  uint8_t  channelMap[5];    //!< The new channel map of the monitored connection
} cmChanMapUpdateEvt_t;

/** 
* @brief Connection Monitor Parameter Update event structure
*/
typedef struct
{
  uint16_t connInterval;     //!< The new connection interval of the monitored connection
  uint16_t connLatency;      //!< The new connection latency of the monitored connection
  uint16_t connTimeout;      //!< The new connection timeout of the monitored connection
  uint16_t winOffset;        //!< The new window offset of the monitored connection
  uint8_t  winSize;          //!< The new window size of the monitored connection
} cmParamUpdateEvt_t;

/** 
* @brief Connection Monitor Update event structure which contains a union of the
*        @ref cmPhyUpdateEvt_t, @ref cmChanMapUpdateEvt_t, @ref cmParamUpdateEvt_t
*/
typedef struct
{
    uint32_t              accessAddr;            //!< Access address of the monitored connection
    uint16_t              connUpdateEvtCnt;      //!< The connection Event counter of the monitored connection
    cmConnUpdateEvtType_e updateType;            //!< The event type @ref cmConnUpdateEvtType_e
    union
    {
        cmPhyUpdateEvt_t     phyUpdateEvt;     //!< Phy update event
        cmChanMapUpdateEvt_t chanMapUpdateEvt; //!< Channel map update event
        cmParamUpdateEvt_t   paramUpdateEvt;   //!< Parameter update event
    } updateEvt;
} cmConnUpdateEvt_t;

/** 
* @brief Connection Monitor start event structure
*/
typedef struct
{
  uint32_t accessAddr;       //!< Access address of the monitored connection
  uint16_t connHandle;       //!< Connection handle of the monitored connection
  uint8_t  addrType;         //!< The address type of the monitored device
  uint8_t  addr[B_ADDR_LEN]; //!< Address of the monitored device
} cmStartEvt_t;

/*
* @brief Connection Monitor stop event structure
*/
typedef struct
{
  uint32_t        accessAddr;       //!< Access address of the monitored connection
  uint16_t        connHandle;       //!< Connection handle of the monitored connection
  uint8_t         addrType;         //!< The address type of the monitored device
  uint8_t         addr[B_ADDR_LEN]; //!< Address of the monitored device
  cmStopReason_e  stopReason;       //!< The reason the monitoring stopped
} cmStopEvt_t;

/**
 * @brief Enumeration for Connection Monitor packet status.
 *
 * This enum defines the possible statuses for packets monitored during BLE connections.
 * It helps distinguish whether a packet was received successfully, received but undetermined,
 * or not received due to a timeout.
 *
 */
typedef enum
{
  CM_PKT_STATUS_NOT_RECEIVED = 0x00, //!< Packet didn't receive
  CM_PKT_VALID_CENTARL       = 0x01, //!< Central Packet received successfully
  CM_PKT_VALID_PERIPHERAL    = 0x02, //!< Peripheral Packet received successfully
  CM_PKT_STATUS_UNDETERMINED = 0x03, //!< Packet received and can't distinguish if it is peripheral or central
} cmPacketStatus_e;

/** 
* @brief Connection Monitor Packet data event structure.
*        The status of the packet will be intiated to @ref CM_PKT_STATUS_NOT_RECEIVED.
*        - If the status of the packet is @ref CM_PKT_VALID_CENTARL it means that the
*          this packet and the data is relevant to the central.
*        - If the status of packet is @ref CM_PKT_VALID_PERIPHERAL it means that the
*          this packet and the data is relevant to the peripheral.
*        - If the status of packet is @ref CM_PKT_STATUS_UNDETERMINED it means that the
*          the controller has recieved a packet but can't tell this packet related to
*          which role.
*/
typedef struct
{
  uint32_t          timestamp;   //!< Timestamp of the received packet
  cmPacketStatus_e  status;      //!< The status of the event @ref cmPacketStatus_e
  int8_t            rssi;        //!< The RSSI of the received packet
  uint8_t           pktLength;   //!< The length of the received packet
  uint8_t           sn:1;        //!< The sequence number of the packet
  uint8_t           nesn:1;      //!< The next sequence number of the packet
  uint8_t           pad:6;       //!< Padding, reserved for future use
} cmPacketData_t;

/** 
* @brief Connection Monitor Report on event structure an open RX window 
*/
typedef struct
{
  uint32_t accessAddr;        //!< Access address of the monitored connection
  uint16_t connHandle;        //!< Connection handle of the monitored connection
  uint16_t connEvtCnt;        //!< The connection event counter of the monitored packets
  uint8_t  channel;           //!< The channel the packet was received on
  cmPacketData_t packets[2];  //!< The packets recieved within the Rx window (Up to two packets)
} cmReportEvt_t;

/** 
* @brief Host Connection Monitor Serving connection update event Callback
*
* @note
* This callback will trigger once the device receive one of the connection updates:
*     1) PHY Update
*     2) Channel Map Update
*     3) Connection parameters Update
*
* @param connHandle - The connection handle
* @param updateEvt  - The update event @ref cmConnUpdateEvt_t
*
* @return None
*/
typedef void(*pfnCmsConnUpdateEvtCB_t)(uint16_t connHandle, cmConnUpdateEvt_t updateEvt);

/**
 * @brief Connection Monitor Serving callbacks
 */
typedef struct
{
  pfnCmsConnUpdateEvtCB_t     pfnCmsConnUpdateEvtCB;
} cmsCBs_t;

/** 
* @brief Host Connection Monitor report event Callback
*
* @note
* This callback will trigger once the device receive the connection's packet with its rssi,
* or when there is a failure in monitoring the peer device.
*
* @param pReportEvt - pointer to the event @ref cmReportEvt_t
*
* @return None
*/
typedef void(*pfnCmReportEvtCB_t)(cmReportEvt_t *pReportEvt);

/** 
* @brief Host Connection Monitor status event Callback
*
* @note
* This callback will trigger once the CM succeed to follow the connection
* ,terminates the connection or to notice that the request received.
*
* @param eventType - The event type
* @param pEvtData - pointer to the event data
*
* @return None
*/
typedef void(*pfnCmConnStatusEvtCB_t)(cmStatusEvtType_e eventType, uint8_t *pEvtData);

/**
 * @brief Connection Monitor callbacks
 */
typedef struct
{
  pfnCmReportEvtCB_t     pfnCmReportEvtCB;
  pfnCmConnStatusEvtCB_t pfnCmConnStatusEvtCB;
} cmCBs_t;

/**
 * @brief Host Connection Monitor Serving get connection data parameters
 */
typedef struct
{
  uint32_t accessAddr;           //!< The Access Addrress will be filled once @ref CMS_GetConnData called
  uint8_t  dataSize;             //!< The size of the data buffer will be filled once @ref CMS_GetConnData called
  uint8_t  *pCmData;             //!< Pointer to the buffer the application allocated and will
                                 //!< be filled once @ref CMS_GetConnData called
} cmsConnDataParams_t;

//
// The connection should be assigned before calling @ref ConnectionMonitor_GetConnData
// The size of the data buffer will be filled once @ref ConnectionMonitor_GetConnData called
// The data buffer will be filled once @ref ConnectionMonitor_GetConnData called
/**
 * @brief Host Connection Monitor start monitor parameters
 */
typedef struct
{
  uint32_t                timeDeltaInUs;       //!< The time in us it took for the data to be transferred from node to another in the system
  uint32_t                timeDeltaMaxErrInUs; //!< The maximum deviation time in us - Not yet implemented and will not affect functionality
  uint32_t                connTimeout;         //!< The supervision connection timeout in ms ; if 0 given - take the supervision connection timeout
                                               //!< from the monitored link
  uint8_t                 maxSyncAttempts;     //!< Number of attempts the device will try to follow before determining the monitoring
                                               //!< process failed or not.
  uint8_t                 cmDataSize;          //!< The stack CM data size
  uint8_t                 *pCmData;            //!< Pointer to the buffer the application allocated
} cmStartMonitorParams_t;

/*******************************************************************************
 * LOCAL VARIABLES
 */

/*******************************************************************************
 * GLOBAL VARIABLES
 */

/*******************************************************************************
 * FUNCTIONS
 */

#endif /* CONNECTION_MONITOR_TYPES_H */
