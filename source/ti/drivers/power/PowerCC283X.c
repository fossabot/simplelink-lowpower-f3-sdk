/*
 * Copyright (c) 2025-2026, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *  ======== PowerCC283X.c ========
 */

#include <stdbool.h>
#include <string.h>

#include <ti/drivers/dpl/DebugP.h>

#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC283X.h>

#include <ti/log/Log.h>

#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/lrfd.h)
#include DeviceFamily_constructPath(driverlib/prcm.h)
#include DeviceFamily_constructPath(driverlib/systimer.h)
#include DeviceFamily_constructPath(inc/hw_ints.h)
#include DeviceFamily_constructPath(inc/hw_types.h)
#include DeviceFamily_constructPath(inc/hw_memmap.h)
#include DeviceFamily_constructPath(inc/hw_prcmull.h)
#include DeviceFamily_constructPath(cmsis/core/cmsis_compiler.h)

/* Type definitions */

/* Forward declarations */
int_fast16_t PowerCC283X_notify(uint_fast16_t eventType);
static void PowerCC283X_setDependencyCount(Power_Resource resourceId, uint8_t count);
static void PowerLPF4_loadPrcmFw(void);
bool PowerCC283X_isValidResourceId(Power_Resource resourceId);

/* Externs */
extern const PowerCC283X_Config PowerCC283X_config;

/* Macro for weak definition of the Power Log module */
Log_MODULE_DEFINE_WEAK(LogModule_Power, Log_MODULE_INIT_SINK_DUMMY);

/* Function Macros */

/* Macro used to extract the resource group from a resource ID */
#define RESOURCE_GROUP(resourceId) ((resourceId)&PowerCC283X_PERIPH_GROUP_M)

/* Macro used to extract the index for the given group from a resource ID */
#define RESOURCE_INDEX(resourceId) ((resourceId)&PowerCC283X_PERIPH_INDEX_M)

/* Static Globals */

/* Array to maintain constraint reference counts.
 * Declare volatile variable to ensure the toolchain does not use
 * stack for the variable and does not optimize this memory allocation away.
 */
static volatile uint8_t constraintCounts[PowerCC283X_NUMCONSTRAINTS];

/* Mask of Power constraints for quick access.
 * Declare volatile variable to ensure the toolchain does not use
 * stack for the variable and does not optimize this memory allocation away.
 */
static volatile uint32_t constraintMask = 0;

/* Arrays to maintain resource dependency reference counts.
 * Each resource group will have an array associated with it, and the arrays can
 * be indexed using the bit index shift value encoded in the resource ID.
 * Declare volatile variable to ensure the toolchain does not use
 * stack for the variable and does not optimize this memory allocation away.
 */
static volatile uint8_t resourceCountsPrcm[PowerCC283X_NUMRESOURCES_PRCM];
static volatile uint8_t resourceCountsLrfd[PowerCC283X_NUMRESOURCES_LRFD];

/* Keeps track of the configured Power policy. Power_idleFunc() will not run
 * the policy if this is set to NULL
 */
static Power_PolicyFxn policyFxn = NULL;

/* Is the Power policy enabled? */
static bool isPolicyEnabled = false;

/* Has the Power driver been initialized */
static bool isInitialized = false;

/* Power state of the system. Idle, active, standby, etc */
static uint8_t powerState = Power_ACTIVE;

/* Event notification list */
static List_List notifyList;

/* Non-static Globals */

/* Interrupt for ClockP and Power policy */
HwiP_Struct clockHwi;

/* ****************** Power APIs ******************** */

/*
 *  ======== Power_init ========
 */
int_fast16_t Power_init(void)
{
    /* If this function has already been called, just return */
    if (isInitialized)
    {
        return Power_SOK;
    }

    isInitialized = true;

    isPolicyEnabled = (PowerCC283X_config.policyFxn != NULL);

    policyFxn = PowerCC283X_config.policyFxn;

    /* Load PRCM FW */
    PowerLPF4_loadPrcmFw();

    /* Need to update the Address for NR to jump to RAM (Earlier hardcoded) */
    HWREG(PRCMULL_BASE + PRCMULL_O_APIMSGBOX) = 0xC0;

    /* Submit API CMD (FW Valid) */
    HWREG(PRCMULL_BASE + PRCMULL_O_APICMD) = 0x1;

    /* Wait while busy. */
    while (HWREG(PRCMULL_BASE + PRCMULL_O_FWSTA) & 0x1) {}

    /* Update Systimer from Initialize to Ready State. */
    HWREG(SYSTIM_BASE + SYSTIM_O_CFG)= SYSTIM_CFG_EN;

    /* Construct the ClockP hwi responsible for timing service events.
     * ClockP will configure the ISR function when it is initialized. So NULL
     * is used here, since it is not expected to be called before ClockP is
     * initialized.
     */
    HwiP_construct(&clockHwi, INT_CPUIRQ5, NULL, NULL);

    /* Make SysTimer halt on CPU debug halt */
    SysTimerSetDebugConfig(SYSTIMER_DEBUG_CONFIG_HALT_STOP);

    return Power_SOK;
}

/*
 *  ======== Power_disablePolicy ========
 *  Do not run the configured policy
 */
bool Power_disablePolicy(void)
{
    bool wasPolicyEnabled = isPolicyEnabled;

    isPolicyEnabled = false;

    return wasPolicyEnabled;
}

/*
 *  ======== Power_enablePolicy ========
 *  Run the configured policy
 */
void Power_enablePolicy(void)
{
    isPolicyEnabled = true;
}

/*
 *  ======== Power_setPolicy ========
 *  Set the Power policy function
 */
void Power_setPolicy(Power_PolicyFxn policy)
{
    policyFxn = policy;
}

/*
 *  ======== Power_getConstraintMask ========
 *  Get a bitmask indicating the constraints that have been registered with
 *  Power.
 */
uint_fast32_t Power_getConstraintMask(void)
{
    return (uint_fast32_t)constraintMask;
}

/*
 *  ======== Power_getDependencyCount ========
 *  Get the count of dependencies that are currently declared upon a resource.
 */
int_fast16_t Power_getDependencyCount(Power_Resource resourceId)
{
    DebugP_assert(PowerCC283X_isValidResourceId(resourceId));

    int_fast16_t result;
    uint8_t index       = RESOURCE_INDEX(resourceId);
    uint_fast16_t group = RESOURCE_GROUP(resourceId);

    if (group == PowerCC283X_PERIPH_GROUP_PRCM)
    {
        result = (int_fast16_t)resourceCountsPrcm[index];
    }
    else if (group == PowerCC283X_PERIPH_GROUP_LRFD)
    {
        result = (int_fast16_t)resourceCountsLrfd[index];
    }
    else
    {
        result = (int_fast16_t)Power_EINVALIDINPUT;
    }

    return result;
}

/*
 *  ======== Power_getConstraintCount ========
 *  Get the count of constraints that are currently set on a certain
 *  operational transition
 */
int_fast16_t Power_getConstraintCount(uint_fast16_t constraintId)
{
    DebugP_assert(constraintId < PowerCC283X_NUMCONSTRAINTS);

    int_fast16_t result = Power_EINVALIDINPUT;

    if (constraintId < PowerCC283X_NUMCONSTRAINTS)
    {
        result = (int_fast16_t)constraintCounts[constraintId];
    }

    return result;
}

/*
 *  ======== Power_getTransitionLatency ========
 *  Get the transition latency for a sleep state.  The latency is reported
 *  in units of microseconds.
 */
uint_fast32_t Power_getTransitionLatency(uint_fast16_t sleepState, uint_fast16_t type)
{
    /* TODO: Make this a dynamic system based on the dynamically computed delta
     * between RTC timeout and re-enabling interrupts
     */

    uint_fast32_t latency = 0;

    if (type == Power_RESUME)
    {
        if (sleepState == PowerLPF4_STANDBY)
        {
            latency = PowerCC283X_RESUMETIMESTANDBY;
        }
    }
    else
    {
        if (sleepState == PowerLPF4_STANDBY)
        {
            latency = PowerCC283X_TOTALTIMESTANDBY;
        }
    }

    return latency;
}

/*
 *  ======== Power_getTransitionState ========
 *  Get the current sleep transition state.
 */
uint_fast16_t Power_getTransitionState(void)
{
    return (uint_fast16_t)powerState;
}

/*
 *  ======== Power_idleFunc ========
 *  Function needs to be plugged into the idle loop.
 *  It calls the configured policy function if it is not NULL.
 */
void Power_idleFunc()
{
    if (policyFxn != NULL && isPolicyEnabled == true)
    {
        (*(policyFxn))();
    }
}

/*
 *  ======== Power_registerNotify ========
 *  Register a function to be called on a specific power event.
 */
int_fast16_t Power_registerNotify(Power_NotifyObj *notifyObj,
                                  uint_fast16_t eventTypes,
                                  Power_NotifyFxn notifyFxn,
                                  uintptr_t clientArg)
{
    int_fast16_t status = Power_SOK;

    /* Check for NULL pointers */
    if ((notifyObj == NULL) || (notifyFxn == NULL))
    {
        Log_printf(LogModule_Power,
                   Log_WARNING,
                   "Power_registerNotify: Notify registration failed due to NULL pointer");

        status = Power_EINVALIDPOINTER;
    }
    else
    {
        notifyObj->eventTypes = eventTypes;
        notifyObj->notifyFxn  = notifyFxn;
        notifyObj->clientArg  = clientArg;

        Log_printf(LogModule_Power,
                   Log_INFO,
                   "Power_registerNotify: Register fxn at address 0x%x with event types 0x%x and clientArg 0x%x",
                   notifyFxn,
                   eventTypes,
                   clientArg);

        /* Place notify object on event notification queue. Assume that
         * List_Elem struct member is the first struct member in
         * Power_NotifyObj.
         */
        List_put(&notifyList, (List_Elem *)notifyObj);
    }

    return status;
}

/*
 *  ======== Power_unregisterNotify ========
 *  Unregister for a power notification.
 *
 */
void Power_unregisterNotify(Power_NotifyObj *notifyObj)
{
    Log_printf(LogModule_Power,
               Log_INFO,
               "Power_unregisterNotify: Unregister fxn at address 0x%x with event types 0x%x and clientArg 0x%x",
               notifyObj->notifyFxn,
               notifyObj->eventTypes,
               notifyObj->clientArg);

    /* Remove notify object from its event queue */
    List_remove(&notifyList, (List_Elem *)notifyObj);
}

/*
 *  ======== Power_setConstraint ========
 *  Declare an operational constraint.
 */
int_fast16_t Power_setConstraint(uint_fast16_t constraintId)
{
    uintptr_t key;

    DebugP_assert(constraintId < PowerCC283X_NUMCONSTRAINTS);

    key = HwiP_disable();

    /* Set the specified constraint in the constraintMask for faster access */
    constraintMask |= 1U << constraintId;

    /* Increment the specified constraint count */
    constraintCounts[constraintId]++;

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_releaseConstraint ========
 *  Release a previously declared constraint.
 */
int_fast16_t Power_releaseConstraint(uint_fast16_t constraintId)
{
    uintptr_t key;

    DebugP_assert(constraintId < PowerCC283X_NUMCONSTRAINTS);

    key = HwiP_disable();

    DebugP_assert(constraintCounts[constraintId] != 0U);

    constraintCounts[constraintId]--;

    /* Only update the constraint mask if we removed the constraint entirely */
    if (constraintCounts[constraintId] == 0U)
    {
        constraintMask &= ~(1U << constraintId);
    }

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_setDependency ========
 *  Declare a dependency upon a resource.
 */
int_fast16_t Power_setDependency(Power_Resource resourceId)
{
    uint8_t previousCount;
    uintptr_t key;
    uint8_t index;

    DebugP_assert(PowerCC283X_isValidResourceId(resourceId));

    key = HwiP_disable();

    /* Buffer previous reference count */
    previousCount = Power_getDependencyCount(resourceId);

    /* Increment reference count */
    PowerCC283X_setDependencyCount(resourceId, previousCount + 1);

    /* If the resource was NOT activated previously ... */
    if (previousCount == 0)
    {
        index = RESOURCE_INDEX(resourceId);
        /* Turn on the peripheral */
        switch (RESOURCE_GROUP(resourceId))
        {
            case PowerCC283X_PERIPH_GROUP_PRCM:
                PRCMEnableClock(index);
                break;
            case PowerCC283X_PERIPH_GROUP_LRFD:
                LRFDSetClockDependency(1U << index, LRFD_CLK_DEP_POWER);
                break;
            default:
                break;
        }
    }

    Log_printf(LogModule_Power,
               Log_INFO,
               "Power_setDependency: Updated resource counter = %d for resource ID = 0x%x",
               Power_getDependencyCount(resourceId), resourceId);

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_releaseDependency ========
 *  Release a previously declared dependency.
 */
int_fast16_t Power_releaseDependency(Power_Resource resourceId)
{
    uint8_t resourceCount;
    uintptr_t key;
    uint8_t index;

    DebugP_assert(PowerCC283X_isValidResourceId(resourceId));

    key = HwiP_disable();

    resourceCount = Power_getDependencyCount(resourceId);

    DebugP_assert(resourceCount != 0);

    /* Decrement the reference count */
    resourceCount--;
    PowerCC283X_setDependencyCount(resourceId, resourceCount);

    /* If this was the last dependency being released.. */
    if (resourceCount == 0)
    {
        index = RESOURCE_INDEX(resourceId);
        /* Turn off the peripheral */
        switch (RESOURCE_GROUP(resourceId))
        {
            case PowerCC283X_PERIPH_GROUP_PRCM:
                PRCMDisableClock(index);
                break;
            case PowerCC283X_PERIPH_GROUP_LRFD:
                LRFDReleaseClockDependency(1U << index, LRFD_CLK_DEP_POWER);
                break;
            default:
                break;
        }
    }

    Log_printf(LogModule_Power,
               Log_INFO,
               "Power_releaseDependency: Updated resource counter = %d for resource ID = 0x%x",
               Power_getDependencyCount(resourceId), resourceId);

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_shutdown ========
 */
int_fast16_t Power_shutdown(uint_fast16_t shutdownState, uint_fast32_t shutdownTime)
{
    /* TODO: Implement function. */
    /* If we get here, failed to shutdown, return error code */
    return Power_EFAIL;
}

/*
 *  ======== Power_sleep ========
 */
int_fast16_t Power_sleep(uint_fast16_t sleepState)
{
    /* TODO: Implement function. */
    return Power_EFAIL;
}

/*
 *  ======== Power_reset ========
 */
void Power_reset(void)
{
    /* TODO: Implement function. */
    while (1) {}
}

/*
 *  ======== PowerCC283X_doWFI ========
 */
void PowerCC283X_doWFI(void)
{
    uint32_t constraints;
    bool idleAllowed;
    bool flashNotNeeded;

    constraints    = Power_getConstraintMask();
    idleAllowed    = (constraints & (1 << PowerLPF4_DISALLOW_IDLE)) == 0;
    flashNotNeeded = (constraints & (1 << PowerLPF4_NEED_FLASH_IN_IDLE)) == 0U;

    if (idleAllowed)
    {

        /* Configure device to turn on/off flash LDO when in idle */
        if (flashNotNeeded)
        {
            /* FlashLdo can be turned off when in IDLE */
            PRCMEnableFlashLdoOffInIdle();
        }
        else
        {
            /* FlashLdo cannot be turned off when in IDLE */
            PRCMDisableFlashLdoOffInIdle();
        }

        /* Enter idle */
        __WFI();
    }
}

/*
 *  ======== PowerLPF4_getResetReason ========
 */
PowerLPF4_ResetReason PowerLPF4_getResetReason(void)
{
    /* TODO: Implement function. */
    return PowerLPF4_RESET_UNKNOWN;
}

/*
 *  ======== PowerLPF4_releaseLatches ========
 */
void PowerLPF4_releaseLatches(void)
{
    /* TODO: Implement function. */
    while (1) {}
}

/*
 *  ======== PowerLPF4_selectLFOSC ========
 */
void PowerLPF4_selectLFOSC(void)
{
    /* TODO: Implement function. */
    while (1) {}
}

/*
 *  ======== PowerLPF4_selectLFXT ========
 */
void PowerLPF4_selectLFXT(void)
{
    /* TODO: Implement function. */
    while (1) {}
}

/*
 *  ======== PowerLPF4_selectEXTLF ========
 */
void PowerLPF4_selectEXTLF(void)
{
    /* TODO: Implement function. */
    while (1) {}
}

/*
 *  ======== PowerCC283X_notify ========
 *  Send notifications to registered clients.
 *  Note: Task scheduling is disabled when this function is called.
 */
int_fast16_t PowerCC283X_notify(uint_fast16_t eventType)
{
    int_fast16_t notifyStatus;
    Power_NotifyFxn notifyFxn;
    uintptr_t clientArg;
    List_Elem *elem;

    /* If queue is empty, return immediately */
    if (!List_empty(&notifyList))
    {
        /* Point to first client notify object */
        elem = List_head(&notifyList);

        /* TODO: Change Power_registerNotify() & Power_unregisterNotify() to use
         * index instead of bitmask and then use multiple lists just like in
         * the TI-RTOS scheduler.
         * We currently walk the entire list 4 times when entering/exiting
         * standby + switching HFXT...
         */

        /* Walk the queue and notify each registered client of the event */
        do
        {
            if ((((Power_NotifyObj *)elem)->eventTypes & eventType) != 0U)
            {
                /* Pull params from notify object */
                notifyFxn = ((Power_NotifyObj *)elem)->notifyFxn;
                clientArg = ((Power_NotifyObj *)elem)->clientArg;

                /* Call the client's notification function */
                Log_printf(LogModule_Power,
                           Log_VERBOSE,
                           "PowerCC283X_notify: Invoking notification fxn at address 0x%x with event type 0x%x and clientArg 0x%x",
                           notifyFxn,
                           eventType,
                           clientArg);

                notifyStatus = (int_fast16_t)(*(Power_NotifyFxn)notifyFxn)(eventType, 0, clientArg);

                /* If client declared error stop all further notifications */
                if (notifyStatus != Power_NOTIFYDONE)
                {
                    Log_printf(LogModule_Power,
                               Log_WARNING,
                               "PowerCC283X_notify: Notification fxn reported error, fxn at address 0x%x with event type 0x%x and notifyStatus 0x%x",
                               notifyFxn,
                               eventType,
                               notifyStatus);

                    return Power_EFAIL;
                }
            }

            /* Get next element in the notification queue */
            elem = List_next(elem);

        } while (elem != NULL);
    }

    return Power_SOK;
}

/*
 *  ======== PowerCC283X_setDependencyCount ========
 */
static void PowerCC283X_setDependencyCount(Power_Resource resourceId, uint8_t count)
{
    uint8_t index       = RESOURCE_INDEX(resourceId);
    uint_fast16_t group = RESOURCE_GROUP(resourceId);

    DebugP_assert(PowerCC283X_isValidResourceId(resourceId));

    if (group == PowerCC283X_PERIPH_GROUP_PRCM)
    {
        resourceCountsPrcm[index] = count;
    }
    else if (group == PowerCC283X_PERIPH_GROUP_LRFD)
    {
        resourceCountsLrfd[index] = count;
    }
}

/*
 *  ======== PowerCC283X_isValidResourceId ========
 */
bool PowerCC283X_isValidResourceId(Power_Resource resourceId)
{
    uint8_t index       = RESOURCE_INDEX(resourceId);
    uint_fast16_t group = RESOURCE_GROUP(resourceId);
    bool result;

    if (resourceId != ((uint32_t)index | (uint32_t)group))
    {
        result = false;
    }
    else
    {
        switch (group)
        {
            case PowerCC283X_PERIPH_GROUP_PRCM:
                result = index < PowerCC283X_NUMRESOURCES_PRCM;
                break;
            case PowerCC283X_PERIPH_GROUP_LRFD:
                result = index < PowerCC283X_NUMRESOURCES_LRFD;
                break;
            default:
                result = false;
                break;
        }
    }

    return result;
}

/*
 *  ======== PowerLPF4_sleep ========
 */
int_fast16_t PowerLPF4_sleep(uint32_t nextEventTimeUs)
{
    /* TODO: Implement function. */
    return Power_EFAIL;
}

static void PowerLPF4_loadPrcmFw(void)
{
    HWREG(PRCMULL_BASE + 4*0x0000) = 0x00010000;
    HWREG(PRCMULL_BASE + 4*0x0001) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x0002) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x0003) = 0x55AAAA55;
    HWREG(PRCMULL_BASE + 4*0x0004) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x0005) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x0006) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x0007) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x0008) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x0009) = 0x00000000;
    HWREG(PRCMULL_BASE + 4*0x000A) = 0x0000FFFF;
    HWREG(PRCMULL_BASE + 4*0x0030) = 0x28011F80;
    HWREG(PRCMULL_BASE + 4*0x0031) = 0x1B909D00;
    HWREG(PRCMULL_BASE + 4*0x0032) = 0x2801E010;
    HWREG(PRCMULL_BASE + 4*0x0033) = 0x3CFCFD80;
    HWREG(PRCMULL_BASE + 4*0x0034) = 0x15CA2800;
    HWREG(PRCMULL_BASE + 4*0x0035) = 0x1F2C1FF0;
    HWREG(PRCMULL_BASE + 4*0x0036) = 0x1F001F2D;
    HWREG(PRCMULL_BASE + 4*0x0037) = 0xA40E1F0E;
    HWREG(PRCMULL_BASE + 4*0x0038) = 0x2802221A;
    HWREG(PRCMULL_BASE + 4*0x0039) = 0x221039F2;
    HWREG(PRCMULL_BASE + 4*0x003A) = 0x38F82802;
    HWREG(PRCMULL_BASE + 4*0x003B) = 0x1F102100;
    HWREG(PRCMULL_BASE + 4*0x003C) = 0x1820CDFE;
    HWREG(PRCMULL_BASE + 4*0x003D) = 0xCFF02800;
    HWREG(PRCMULL_BASE + 4*0x003E) = 0x28011120;
    HWREG(PRCMULL_BASE + 4*0x003F) = 0x0060E000;
    HWREG(PRCMULL_BASE + 4*0x0040) = 0x3FDF2801;
    HWREG(PRCMULL_BASE + 4*0x0041) = 0x3F572801;
    HWREG(PRCMULL_BASE + 4*0x0042) = 0x3F682801;
    HWREG(PRCMULL_BASE + 4*0x0043) = 0x3F702801;
    HWREG(PRCMULL_BASE + 4*0x0044) = 0x3F792801;
    HWREG(PRCMULL_BASE + 4*0x0045) = 0x3F912801;
    HWREG(PRCMULL_BASE + 4*0x0046) = 0x3FA32801;
    HWREG(PRCMULL_BASE + 4*0x0047) = 0x3FAD2801;
    HWREG(PRCMULL_BASE + 4*0x0048) = 0x3FB42801;
    HWREG(PRCMULL_BASE + 4*0x0049) = 0x3FCD2801;
    HWREG(PRCMULL_BASE + 4*0x004A) = 0x3FCB2801;
    HWREG(PRCMULL_BASE + 4*0x004B) = 0x3FC92801;
    HWREG(PRCMULL_BASE + 4*0x004C) = 0x3FC72801;
    HWREG(PRCMULL_BASE + 4*0x004D) = 0x3FC52801;
    HWREG(PRCMULL_BASE + 4*0x004E) = 0x3FC32801;
    HWREG(PRCMULL_BASE + 4*0x004F) = 0x3FE12801;
    HWREG(PRCMULL_BASE + 4*0x0050) = 0x3F8D2802;
    HWREG(PRCMULL_BASE + 4*0x0051) = 0x3F9D2802;
    HWREG(PRCMULL_BASE + 4*0x0052) = 0x3FA52802;
    HWREG(PRCMULL_BASE + 4*0x0053) = 0x3F0C2801;
    HWREG(PRCMULL_BASE + 4*0x0054) = 0x3F272801;
    HWREG(PRCMULL_BASE + 4*0x0055) = 0x3FF52800;
    HWREG(PRCMULL_BASE + 4*0x0056) = 0x3FC72800;
    HWREG(PRCMULL_BASE + 4*0x0057) = 0x3FAE2800;
    HWREG(PRCMULL_BASE + 4*0x0058) = 0x3F952800;
    HWREG(PRCMULL_BASE + 4*0x0059) = 0x3F9E2800;
    HWREG(PRCMULL_BASE + 4*0x005A) = 0x00003F48;
    HWREG(PRCMULL_BASE + 4*0x005B) = 0x00003F51;
    HWREG(PRCMULL_BASE + 4*0x005C) = 0x00003F52;
    HWREG(PRCMULL_BASE + 4*0x005D) = 0x00003F55;
    HWREG(PRCMULL_BASE + 4*0x005E) = 0x3FD12800;
    HWREG(PRCMULL_BASE + 4*0x005F) = 0x00003F62;
    HWREG(PRCMULL_BASE + 4*0x0060) = 0x00003F73;
    HWREG(PRCMULL_BASE + 4*0x0061) = 0x00003F12;
    HWREG(PRCMULL_BASE + 4*0x0062) = 0x00003F28;
    HWREG(PRCMULL_BASE + 4*0x0063) = 0x3F992801;
    HWREG(PRCMULL_BASE + 4*0x0064) = 0x3F972801;
    HWREG(PRCMULL_BASE + 4*0x0065) = 0x28011F7E;
    HWREG(PRCMULL_BASE + 4*0x0066) = 0x1B909E20;
    HWREG(PRCMULL_BASE + 4*0x0067) = 0x000096E2;
    HWREG(PRCMULL_BASE + 4*0x0068) = 0xE01037F2;
    HWREG(PRCMULL_BASE + 4*0x0069) = 0x00021B90;
    HWREG(PRCMULL_BASE + 4*0x006A) = 0x15D82800;
    HWREG(PRCMULL_BASE + 4*0x006B) = 0x3F892801;
    HWREG(PRCMULL_BASE + 4*0x006C) = 0x9D902801;
    HWREG(PRCMULL_BASE + 4*0x006D) = 0x1F7E1B90;
    HWREG(PRCMULL_BASE + 4*0x006E) = 0x9ED02801;
    HWREG(PRCMULL_BASE + 4*0x006F) = 0x217E1B90;
    HWREG(PRCMULL_BASE + 4*0x0070) = 0x9E202801;
    HWREG(PRCMULL_BASE + 4*0x0071) = 0x96E21B90;
    HWREG(PRCMULL_BASE + 4*0x0072) = 0x37F20000;
    HWREG(PRCMULL_BASE + 4*0x0073) = 0x1B90E010;
    HWREG(PRCMULL_BASE + 4*0x0074) = 0x9D602801;
    HWREG(PRCMULL_BASE + 4*0x0075) = 0x00021B90;
    HWREG(PRCMULL_BASE + 4*0x0076) = 0x15F02800;
    HWREG(PRCMULL_BASE + 4*0x0077) = 0x3F712801;
    HWREG(PRCMULL_BASE + 4*0x0078) = 0x9D902801;
    HWREG(PRCMULL_BASE + 4*0x0079) = 0x28011B90;
    HWREG(PRCMULL_BASE + 4*0x007A) = 0x1B909ED0;
    HWREG(PRCMULL_BASE + 4*0x007B) = 0x15CA2800;
    HWREG(PRCMULL_BASE + 4*0x007C) = 0x9D602801;
    HWREG(PRCMULL_BASE + 4*0x007D) = 0x00021B90;
    HWREG(PRCMULL_BASE + 4*0x007E) = 0x47802840;
    HWREG(PRCMULL_BASE + 4*0x007F) = 0x3805F010;
    HWREG(PRCMULL_BASE + 4*0x0080) = 0x4E802841;
    HWREG(PRCMULL_BASE + 4*0x0081) = 0x3F5D2801;
    HWREG(PRCMULL_BASE + 4*0x0082) = 0x28011F7D;
    HWREG(PRCMULL_BASE + 4*0x0083) = 0x00003F5A;
    HWREG(PRCMULL_BASE + 4*0x0084) = 0x3F572801;
    HWREG(PRCMULL_BASE + 4*0x0085) = 0x47802840;
    HWREG(PRCMULL_BASE + 4*0x0086) = 0x28011B50;
    HWREG(PRCMULL_BASE + 4*0x0087) = 0x1F123F52;
    HWREG(PRCMULL_BASE + 4*0x0088) = 0x1F021F13;
    HWREG(PRCMULL_BASE + 4*0x0089) = 0xA40E1F03;
    HWREG(PRCMULL_BASE + 4*0x008A) = 0x1F131F12;
    HWREG(PRCMULL_BASE + 4*0x008B) = 0x21032102;
    HWREG(PRCMULL_BASE + 4*0x008C) = 0x284018A0;
    HWREG(PRCMULL_BASE + 4*0x008D) = 0x18B04F80;
    HWREG(PRCMULL_BASE + 4*0x008E) = 0x4F902840;
    HWREG(PRCMULL_BASE + 4*0x008F) = 0x3F412801;
    HWREG(PRCMULL_BASE + 4*0x0090) = 0x47802840;
    HWREG(PRCMULL_BASE + 4*0x0091) = 0x28419071;
    HWREG(PRCMULL_BASE + 4*0x0092) = 0x28414C81;
    HWREG(PRCMULL_BASE + 4*0x0093) = 0x9FF14CC0;
    HWREG(PRCMULL_BASE + 4*0x0094) = 0x49512882;
    HWREG(PRCMULL_BASE + 4*0x0095) = 0x39FF22F1;
    HWREG(PRCMULL_BASE + 4*0x0096) = 0x28829021;
    HWREG(PRCMULL_BASE + 4*0x0097) = 0x22F14941;
    HWREG(PRCMULL_BASE + 4*0x0098) = 0x280139FF;
    HWREG(PRCMULL_BASE + 4*0x0099) = 0x28403F2E;
    HWREG(PRCMULL_BASE + 4*0x009A) = 0x9FF14790;
    HWREG(PRCMULL_BASE + 4*0x009B) = 0x28800601;
    HWREG(PRCMULL_BASE + 4*0x009C) = 0x3806D000;
    HWREG(PRCMULL_BASE + 4*0x009D) = 0x47822840;
    HWREG(PRCMULL_BASE + 4*0x009E) = 0x28010212;
    HWREG(PRCMULL_BASE + 4*0x009F) = 0x01123F22;
    HWREG(PRCMULL_BASE + 4*0x00A0) = 0x4F822840;
    HWREG(PRCMULL_BASE + 4*0x00A1) = 0x3F1D2801;
    HWREG(PRCMULL_BASE + 4*0x00A2) = 0x3F1B2801;
    HWREG(PRCMULL_BASE + 4*0x00A3) = 0x91029011;
    HWREG(PRCMULL_BASE + 4*0x00A4) = 0x04211181;
    HWREG(PRCMULL_BASE + 4*0x00A5) = 0x1FA71BA1;
    HWREG(PRCMULL_BASE + 4*0x00A6) = 0x21A70000;
    HWREG(PRCMULL_BASE + 4*0x00A7) = 0x2801A40E;
    HWREG(PRCMULL_BASE + 4*0x00A8) = 0x90213F10;
    HWREG(PRCMULL_BASE + 4*0x00A9) = 0x11819102;
    HWREG(PRCMULL_BASE + 4*0x00AA) = 0x1BA10421;
    HWREG(PRCMULL_BASE + 4*0x00AB) = 0x00001FA7;
    HWREG(PRCMULL_BASE + 4*0x00AC) = 0x212721A7;
    HWREG(PRCMULL_BASE + 4*0x00AD) = 0x2801A40E;
    HWREG(PRCMULL_BASE + 4*0x00AE) = 0x28403F04;
    HWREG(PRCMULL_BASE + 4*0x00AF) = 0x90714780;
    HWREG(PRCMULL_BASE + 4*0x00B0) = 0x04101140;
    HWREG(PRCMULL_BASE + 4*0x00B1) = 0x1F011F11;
    HWREG(PRCMULL_BASE + 4*0x00B2) = 0x90221BB0;
    HWREG(PRCMULL_BASE + 4*0x00B3) = 0x37F20000;
    HWREG(PRCMULL_BASE + 4*0x00B4) = 0xA40E21BC;
    HWREG(PRCMULL_BASE + 4*0x00B5) = 0x1F112101;
    HWREG(PRCMULL_BASE + 4*0x00B6) = 0x9031CDFE;
    HWREG(PRCMULL_BASE + 4*0x00B7) = 0x18611BB1;
    HWREG(PRCMULL_BASE + 4*0x00B8) = 0x4F812840;
    HWREG(PRCMULL_BASE + 4*0x00B9) = 0x3FED2800;
    HWREG(PRCMULL_BASE + 4*0x00BA) = 0x43052881;
    HWREG(PRCMULL_BASE + 4*0x00BB) = 0xCFC319C3;
    HWREG(PRCMULL_BASE + 4*0x00BC) = 0x3D0FF045;
    HWREG(PRCMULL_BASE + 4*0x00BD) = 0x04351105;
    HWREG(PRCMULL_BASE + 4*0x00BE) = 0x1F4421C2;
    HWREG(PRCMULL_BASE + 4*0x00BF) = 0x1BC51F45;
    HWREG(PRCMULL_BASE + 4*0x00C0) = 0x280421C2;
    HWREG(PRCMULL_BASE + 4*0x00C1) = 0x00009000;
    HWREG(PRCMULL_BASE + 4*0x00C2) = 0x1F4637F0;
    HWREG(PRCMULL_BASE + 4*0x00C3) = 0x3FD92800;
    HWREG(PRCMULL_BASE + 4*0x00C4) = 0x21452144;
    HWREG(PRCMULL_BASE + 4*0x00C5) = 0x1FFF2146;
    HWREG(PRCMULL_BASE + 4*0x00C6) = 0x3FD32800;
    HWREG(PRCMULL_BASE + 4*0x00C7) = 0x43052881;
    HWREG(PRCMULL_BASE + 4*0x00C8) = 0xCE7319C3;
    HWREG(PRCMULL_BASE + 4*0x00C9) = 0x3DF5F045;
    HWREG(PRCMULL_BASE + 4*0x00CA) = 0x04351135;
    HWREG(PRCMULL_BASE + 4*0x00CB) = 0x1F451F44;
    HWREG(PRCMULL_BASE + 4*0x00CC) = 0x28041BC5;
    HWREG(PRCMULL_BASE + 4*0x00CD) = 0x00009000;
    HWREG(PRCMULL_BASE + 4*0x00CE) = 0x1F4637F0;
    HWREG(PRCMULL_BASE + 4*0x00CF) = 0x3FC12800;
    HWREG(PRCMULL_BASE + 4*0x00D0) = 0x47852840;
    HWREG(PRCMULL_BASE + 4*0x00D1) = 0xD0052801;
    HWREG(PRCMULL_BASE + 4*0x00D2) = 0x1F70380C;
    HWREG(PRCMULL_BASE + 4*0x00D3) = 0x19731F71;
    HWREG(PRCMULL_BASE + 4*0x00D4) = 0x1125C833;
    HWREG(PRCMULL_BASE + 4*0x00D5) = 0x1B730453;
    HWREG(PRCMULL_BASE + 4*0x00D6) = 0x21771F77;
    HWREG(PRCMULL_BASE + 4*0x00D7) = 0x3FB12800;
    HWREG(PRCMULL_BASE + 4*0x00D8) = 0x28002171;
    HWREG(PRCMULL_BASE + 4*0x00D9) = 0x1F403FAE;
    HWREG(PRCMULL_BASE + 4*0x00DA) = 0x254A1F4E;
    HWREG(PRCMULL_BASE + 4*0x00DB) = 0x1F4B1F4D;
    HWREG(PRCMULL_BASE + 4*0x00DC) = 0x9FF028FF;
    HWREG(PRCMULL_BASE + 4*0x00DD) = 0x4C402842;
    HWREG(PRCMULL_BASE + 4*0x00DE) = 0x93602836;
    HWREG(PRCMULL_BASE + 4*0x00DF) = 0x4C502842;
    HWREG(PRCMULL_BASE + 4*0x00E0) = 0x1F431F47;
    HWREG(PRCMULL_BASE + 4*0x00E1) = 0x1F7A214F;
    HWREG(PRCMULL_BASE + 4*0x00E2) = 0x93C01F49;
    HWREG(PRCMULL_BASE + 4*0x00E3) = 0x37F00000;
    HWREG(PRCMULL_BASE + 4*0x00E4) = 0x1F4C2149;
    HWREG(PRCMULL_BASE + 4*0x00E5) = 0x000093C0;
    HWREG(PRCMULL_BASE + 4*0x00E6) = 0x1FF737F0;
    HWREG(PRCMULL_BASE + 4*0x00E7) = 0x3F912800;
    HWREG(PRCMULL_BASE + 4*0x00E8) = 0x21472140;
    HWREG(PRCMULL_BASE + 4*0x00E9) = 0x21492143;
    HWREG(PRCMULL_BASE + 4*0x00EA) = 0x214D214C;
    HWREG(PRCMULL_BASE + 4*0x00EB) = 0x214E214B;
    HWREG(PRCMULL_BASE + 4*0x00EC) = 0x3F872800;
    HWREG(PRCMULL_BASE + 4*0x00ED) = 0x43052881;
    HWREG(PRCMULL_BASE + 4*0x00EE) = 0x12300350;
    HWREG(PRCMULL_BASE + 4*0x00EF) = 0x15632802;
    HWREG(PRCMULL_BASE + 4*0x00F0) = 0x28000351;
    HWREG(PRCMULL_BASE + 4*0x00F1) = 0x9012C071;
    HWREG(PRCMULL_BASE + 4*0x00F2) = 0x06230D12;
    HWREG(PRCMULL_BASE + 4*0x00F3) = 0x90020E13;
    HWREG(PRCMULL_BASE + 4*0x00F4) = 0x4F832840;
    HWREG(PRCMULL_BASE + 4*0x00F5) = 0x4F922840;
    HWREG(PRCMULL_BASE + 4*0x00F6) = 0x28403F74;
    HWREG(PRCMULL_BASE + 4*0x00F7) = 0x28024780;
    HWREG(PRCMULL_BASE + 4*0x00F8) = 0x90021563;
    HWREG(PRCMULL_BASE + 4*0x00F9) = 0x4F832840;
    HWREG(PRCMULL_BASE + 4*0x00FA) = 0x4F922840;
    HWREG(PRCMULL_BASE + 4*0x00FB) = 0x28813F6A;
    HWREG(PRCMULL_BASE + 4*0x00FC) = 0x11104300;
    HWREG(PRCMULL_BASE + 4*0x00FD) = 0x15632802;
    HWREG(PRCMULL_BASE + 4*0x00FE) = 0x28409002;
    HWREG(PRCMULL_BASE + 4*0x00FF) = 0x28404F84;
    HWREG(PRCMULL_BASE + 4*0x0100) = 0x3F5F4F92;
    HWREG(PRCMULL_BASE + 4*0x0101) = 0x43052881;
    HWREG(PRCMULL_BASE + 4*0x0102) = 0x43162881;
    HWREG(PRCMULL_BASE + 4*0x0103) = 0x12300350;
    HWREG(PRCMULL_BASE + 4*0x0104) = 0x15632802;
    HWREG(PRCMULL_BASE + 4*0x0105) = 0x28000351;
    HWREG(PRCMULL_BASE + 4*0x0106) = 0x9012C071;
    HWREG(PRCMULL_BASE + 4*0x0107) = 0x03240D12;
    HWREG(PRCMULL_BASE + 4*0x0108) = 0x06430054;
    HWREG(PRCMULL_BASE + 4*0x0109) = 0x3802D016;
    HWREG(PRCMULL_BASE + 4*0x010A) = 0x11830423;
    HWREG(PRCMULL_BASE + 4*0x010B) = 0x1BA30403;
    HWREG(PRCMULL_BASE + 4*0x010C) = 0x00001FA7;
    HWREG(PRCMULL_BASE + 4*0x010D) = 0x3F4521A7;
    HWREG(PRCMULL_BASE + 4*0x010E) = 0x43002881;
    HWREG(PRCMULL_BASE + 4*0x010F) = 0x43152881;
    HWREG(PRCMULL_BASE + 4*0x0110) = 0x43262881;
    HWREG(PRCMULL_BASE + 4*0x0111) = 0x15632802;
    HWREG(PRCMULL_BASE + 4*0x0112) = 0x00510361;
    HWREG(PRCMULL_BASE + 4*0x0113) = 0x06560613;
    HWREG(PRCMULL_BASE + 4*0x0114) = 0x11830463;
    HWREG(PRCMULL_BASE + 4*0x0115) = 0x1BA00430;
    HWREG(PRCMULL_BASE + 4*0x0116) = 0x00001FA7;
    HWREG(PRCMULL_BASE + 4*0x0117) = 0x3F3121A7;
    HWREG(PRCMULL_BASE + 4*0x0118) = 0x43002881;
    HWREG(PRCMULL_BASE + 4*0x0119) = 0xCE111981;
    HWREG(PRCMULL_BASE + 4*0x011A) = 0x04101110;
    HWREG(PRCMULL_BASE + 4*0x011B) = 0x1F801B80;
    HWREG(PRCMULL_BASE + 4*0x011C) = 0x28401841;
    HWREG(PRCMULL_BASE + 4*0x011D) = 0x3F254F81;
    HWREG(PRCMULL_BASE + 4*0x011E) = 0x47802840;
    HWREG(PRCMULL_BASE + 4*0x011F) = 0x47912840;
    HWREG(PRCMULL_BASE + 4*0x0120) = 0x156D2802;
    HWREG(PRCMULL_BASE + 4*0x0121) = 0x4F842840;
    HWREG(PRCMULL_BASE + 4*0x0122) = 0x1F113F1C;
    HWREG(PRCMULL_BASE + 4*0x0123) = 0x1FBC1F01;
    HWREG(PRCMULL_BASE + 4*0x0124) = 0x28FF21BC;
    HWREG(PRCMULL_BASE + 4*0x0125) = 0x1BB19FF1;
    HWREG(PRCMULL_BASE + 4*0x0126) = 0x900128F0;
    HWREG(PRCMULL_BASE + 4*0x0127) = 0x93001BB1;
    HWREG(PRCMULL_BASE + 4*0x0128) = 0x37F00000;
    HWREG(PRCMULL_BASE + 4*0x0129) = 0x1F112101;
    HWREG(PRCMULL_BASE + 4*0x012A) = 0x4470CDFE;
    HWREG(PRCMULL_BASE + 4*0x012B) = 0x28401861;
    HWREG(PRCMULL_BASE + 4*0x012C) = 0x28024F81;
    HWREG(PRCMULL_BASE + 4*0x012D) = 0x4460156D;
    HWREG(PRCMULL_BASE + 4*0x012E) = 0x28400804;
    HWREG(PRCMULL_BASE + 4*0x012F) = 0x3F014F84;
    HWREG(PRCMULL_BASE + 4*0x0130) = 0x2BFE1FF0;
    HWREG(PRCMULL_BASE + 4*0x0131) = 0x03013F0B;
    HWREG(PRCMULL_BASE + 4*0x0132) = 0x00001BA0;
    HWREG(PRCMULL_BASE + 4*0x0133) = 0x03431894;
    HWREG(PRCMULL_BASE + 4*0x0134) = 0x3902D010;
    HWREG(PRCMULL_BASE + 4*0x0135) = 0x12831183;
    HWREG(PRCMULL_BASE + 4*0x0136) = 0x03020002;
    HWREG(PRCMULL_BASE + 4*0x0137) = 0x16320313;
    HWREG(PRCMULL_BASE + 4*0x0138) = 0x12840324;
    HWREG(PRCMULL_BASE + 4*0x0139) = 0x12820302;
    HWREG(PRCMULL_BASE + 4*0x013A) = 0x08241632;
    HWREG(PRCMULL_BASE + 4*0x013B) = 0x12830302;
    HWREG(PRCMULL_BASE + 4*0x013C) = 0x08241632;
    HWREG(PRCMULL_BASE + 4*0x013D) = 0x12820302;
    HWREG(PRCMULL_BASE + 4*0x013E) = 0x11821632;
    HWREG(PRCMULL_BASE + 4*0x013F) = 0x00020824;
    HWREG(PRCMULL_BASE + 4*0x0140) = 0x1F102100;
    HWREG(PRCMULL_BASE + 4*0x0141) = 0x1F19CDFE;
    HWREG(PRCMULL_BASE + 4*0x0142) = 0xA40E1F09;
    HWREG(PRCMULL_BASE + 4*0x0143) = 0x212B1F2A;
    HWREG(PRCMULL_BASE + 4*0x0144) = 0x1F221F25;
    HWREG(PRCMULL_BASE + 4*0x0145) = 0x1F291F28;
    HWREG(PRCMULL_BASE + 4*0x0146) = 0x1F192109;
    HWREG(PRCMULL_BASE + 4*0x0147) = 0x2801CDFE;
    HWREG(PRCMULL_BASE + 4*0x0148) = 0x28019181;
    HWREG(PRCMULL_BASE + 4*0x0149) = 0x031091D2;
    HWREG(PRCMULL_BASE + 4*0x014A) = 0xE0101B90;
    HWREG(PRCMULL_BASE + 4*0x014B) = 0x3CFD0C20;
    HWREG(PRCMULL_BASE + 4*0x014C) = 0x1B90E000;
    HWREG(PRCMULL_BASE + 4*0x014D) = 0xE102E101;
    HWREG(PRCMULL_BASE + 4*0x014E) = 0xFD812801;
    HWREG(PRCMULL_BASE + 4*0x014F) = 0x19303CF5;
    HWREG(PRCMULL_BASE + 4*0x0150) = 0x9EC12BFF;
    HWREG(PRCMULL_BASE + 4*0x0151) = 0x1B300610;
    HWREG(PRCMULL_BASE + 4*0x0152) = 0x38FF2235;
    HWREG(PRCMULL_BASE + 4*0x0153) = 0x9C312171;
    HWREG(PRCMULL_BASE + 4*0x0154) = 0x4C112844;
    HWREG(PRCMULL_BASE + 4*0x0155) = 0x972C2805;
    HWREG(PRCMULL_BASE + 4*0x0156) = 0x1F3A2123;
    HWREG(PRCMULL_BASE + 4*0x0157) = 0x1F2C212C;
    HWREG(PRCMULL_BASE + 4*0x0158) = 0x9F492877;
    HWREG(PRCMULL_BASE + 4*0x0159) = 0x2811910B;
    HWREG(PRCMULL_BASE + 4*0x015A) = 0x1F189866;
    HWREG(PRCMULL_BASE + 4*0x015B) = 0xCDFE1F08;
    HWREG(PRCMULL_BASE + 4*0x015C) = 0x90090066;
    HWREG(PRCMULL_BASE + 4*0x015D) = 0x28449008;
    HWREG(PRCMULL_BASE + 4*0x015E) = 0x1F234C18;
    HWREG(PRCMULL_BASE + 4*0x015F) = 0x1F211F20;
    HWREG(PRCMULL_BASE + 4*0x0160) = 0x2108213A;
    HWREG(PRCMULL_BASE + 4*0x0161) = 0xCDFE1F18;
    HWREG(PRCMULL_BASE + 4*0x0162) = 0xD0174547;
    HWREG(PRCMULL_BASE + 4*0x0163) = 0x1F78380C;
    HWREG(PRCMULL_BASE + 4*0x0164) = 0x1F709025;
    HWREG(PRCMULL_BASE + 4*0x0165) = 0x19731F71;
    HWREG(PRCMULL_BASE + 4*0x0166) = 0x1125C833;
    HWREG(PRCMULL_BASE + 4*0x0167) = 0x1B730453;
    HWREG(PRCMULL_BASE + 4*0x0168) = 0x21771F77;
    HWREG(PRCMULL_BASE + 4*0x0169) = 0x38FF2230;
    HWREG(PRCMULL_BASE + 4*0x016A) = 0x38FF2232;
    HWREG(PRCMULL_BASE + 4*0x016B) = 0x1F2C1830;
    HWREG(PRCMULL_BASE + 4*0x016C) = 0xCC002803;
    HWREG(PRCMULL_BASE + 4*0x016D) = 0xD0273928;
    HWREG(PRCMULL_BASE + 4*0x016E) = 0x19303816;
    HWREG(PRCMULL_BASE + 4*0x016F) = 0x1B30A030;
    HWREG(PRCMULL_BASE + 4*0x0170) = 0x38FF2234;
    HWREG(PRCMULL_BASE + 4*0x0171) = 0x9D002801;
    HWREG(PRCMULL_BASE + 4*0x0172) = 0xE0101B90;
    HWREG(PRCMULL_BASE + 4*0x0173) = 0xFD802801;
    HWREG(PRCMULL_BASE + 4*0x0174) = 0x1FBC3CFC;
    HWREG(PRCMULL_BASE + 4*0x0175) = 0x280121BC;
    HWREG(PRCMULL_BASE + 4*0x0176) = 0x1B909D80;
    HWREG(PRCMULL_BASE + 4*0x0177) = 0x2801E010;
    HWREG(PRCMULL_BASE + 4*0x0178) = 0x3CFCFDD0;
    HWREG(PRCMULL_BASE + 4*0x0179) = 0x38ACD047;
    HWREG(PRCMULL_BASE + 4*0x017A) = 0x1F431F47;
    HWREG(PRCMULL_BASE + 4*0x017B) = 0x1F4C1F49;
    HWREG(PRCMULL_BASE + 4*0x017C) = 0x9F301F4E;
    HWREG(PRCMULL_BASE + 4*0x017D) = 0x37F00000;
    HWREG(PRCMULL_BASE + 4*0x017E) = 0x21432147;
    HWREG(PRCMULL_BASE + 4*0x017F) = 0x214C2149;
    HWREG(PRCMULL_BASE + 4*0x0180) = 0x3F9E214E;
    HWREG(PRCMULL_BASE + 4*0x0181) = 0x21222125;
    HWREG(PRCMULL_BASE + 4*0x0182) = 0x21292128;
    HWREG(PRCMULL_BASE + 4*0x0183) = 0x1F342178;
    HWREG(PRCMULL_BASE + 4*0x0184) = 0x2233212D;
    HWREG(PRCMULL_BASE + 4*0x0185) = 0x18D038FF;
    HWREG(PRCMULL_BASE + 4*0x0186) = 0xC0702800;
    HWREG(PRCMULL_BASE + 4*0x0187) = 0x39FCF070;
    HWREG(PRCMULL_BASE + 4*0x0188) = 0xA0301930;
    HWREG(PRCMULL_BASE + 4*0x0189) = 0x22341B30;
    HWREG(PRCMULL_BASE + 4*0x018A) = 0x900038FF;
    HWREG(PRCMULL_BASE + 4*0x018B) = 0x90101B90;
    HWREG(PRCMULL_BASE + 4*0x018C) = 0x28011B90;
    HWREG(PRCMULL_BASE + 4*0x018D) = 0x1B909020;
    HWREG(PRCMULL_BASE + 4*0x018E) = 0x2801E100;
    HWREG(PRCMULL_BASE + 4*0x018F) = 0x3CFCFDA0;
    HWREG(PRCMULL_BASE + 4*0x0190) = 0x00009400;
    HWREG(PRCMULL_BASE + 4*0x0191) = 0x903037F0;
    HWREG(PRCMULL_BASE + 4*0x0192) = 0xE0101B90;
    HWREG(PRCMULL_BASE + 4*0x0193) = 0x9085F080;
    HWREG(PRCMULL_BASE + 4*0x0194) = 0x90703CFC;
    HWREG(PRCMULL_BASE + 4*0x0195) = 0xE0101B90;
    HWREG(PRCMULL_BASE + 4*0x0196) = 0x3F332BFF;
    HWREG(PRCMULL_BASE + 4*0x0197) = 0x47802840;
    HWREG(PRCMULL_BASE + 4*0x0198) = 0x155A2803;
    HWREG(PRCMULL_BASE + 4*0x0199) = 0x28401F2D;
    HWREG(PRCMULL_BASE + 4*0x019A) = 0xF0114791;
    HWREG(PRCMULL_BASE + 4*0x019B) = 0x38292BFF;
    HWREG(PRCMULL_BASE + 4*0x019C) = 0x1F041F14;
    HWREG(PRCMULL_BASE + 4*0x019D) = 0x1F14A40E;
    HWREG(PRCMULL_BASE + 4*0x019E) = 0xCDFE2104;
    HWREG(PRCMULL_BASE + 4*0x019F) = 0x3F212BFF;
    HWREG(PRCMULL_BASE + 4*0x01A0) = 0x47802840;
    HWREG(PRCMULL_BASE + 4*0x01A1) = 0x155E2803;
    HWREG(PRCMULL_BASE + 4*0x01A2) = 0x1F15A40E;
    HWREG(PRCMULL_BASE + 4*0x01A3) = 0xCDFE2105;
    HWREG(PRCMULL_BASE + 4*0x01A4) = 0x3F172BFF;
    HWREG(PRCMULL_BASE + 4*0x01A5) = 0x47802840;
    HWREG(PRCMULL_BASE + 4*0x01A6) = 0x2B0F19C1;
    HWREG(PRCMULL_BASE + 4*0x01A7) = 0x1160C0F1;
    HWREG(PRCMULL_BASE + 4*0x01A8) = 0x1BC00410;
    HWREG(PRCMULL_BASE + 4*0x01A9) = 0x1F061F16;
    HWREG(PRCMULL_BASE + 4*0x01AA) = 0x1F16A40E;
    HWREG(PRCMULL_BASE + 4*0x01AB) = 0xCDFE2106;
    HWREG(PRCMULL_BASE + 4*0x01AC) = 0x3F072BFF;
    HWREG(PRCMULL_BASE + 4*0x01AD) = 0x08011881;
    HWREG(PRCMULL_BASE + 4*0x01AE) = 0x00021BE1;
    HWREG(PRCMULL_BASE + 4*0x01AF) = 0x08011871;
    HWREG(PRCMULL_BASE + 4*0x01B0) = 0x1F151BD1;
    HWREG(PRCMULL_BASE + 4*0x01B1) = 0x00021F05;
    HWREG(PRCMULL_BASE + 4*0x01B2) = 0x280F1850;
    HWREG(PRCMULL_BASE + 4*0x01B3) = 0x1BA0C000;
    HWREG(PRCMULL_BASE + 4*0x01B4) = 0x21A71FA7;
    HWREG(PRCMULL_BASE + 4*0x01B5) = 0x21260000;
    HWREG(PRCMULL_BASE + 4*0x01B6) = 0x230E3F00;
    HWREG(PRCMULL_BASE + 4*0x01B7) = 0xCDFE390C;
    HWREG(PRCMULL_BASE + 4*0x01B8) = 0x9FF028FF;
    HWREG(PRCMULL_BASE + 4*0x01B9) = 0x1B101F8C;
    HWREG(PRCMULL_BASE + 4*0x01BA) = 0x9FF028FF;
    HWREG(PRCMULL_BASE + 4*0x01BB) = 0x1B10218C;
    HWREG(PRCMULL_BASE + 4*0x01BC) = 0x3FF42BFC;
    HWREG(PRCMULL_BASE + 4*0x01BD) = 0x1F1ECDFE;
    HWREG(PRCMULL_BASE + 4*0x01BE) = 0x39042237;
    HWREG(PRCMULL_BASE + 4*0x01BF) = 0x2BFC218B;
    HWREG(PRCMULL_BASE + 4*0x01C0) = 0x1F8B3FED;
    HWREG(PRCMULL_BASE + 4*0x01C1) = 0x3FEA2BFC;
}