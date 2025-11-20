/* ZBOSS Zigbee software protocol stack
 *
 * Copyright (c) 2012-2024 DSR Corporation, Denver CO, USA.
 * www.dsr-zboss.com
 * www.dsr-corporation.com
 * All rights reserved.
 *
 * This is unpublished proprietary source code of DSR Corporation
 * The copyright notice does not evidence any actual or intended
 * publication of such source code.
 *
 * ZBOSS is a registered trademark of Data Storage Research LLC d/b/a DSR
 * Corporation
 *
 * Commercial Usage
 * Licensees holding valid DSR Commercial licenses may use
 * this file in accordance with the DSR Commercial License
 * Agreement provided with the Software or, alternatively, in accordance
 * with the terms contained in a written agreement between you and
 * DSR.
 */

#ifndef ZB_DIAG_CORE_WATCHDOG_H
#define ZB_DIAG_CORE_WATCHDOG_H

/**
 * Initialize and run ZBOSS core watchdog
 *
 * Should be called from @see zb_diag_init()
 */
void zb_diag_core_watchdog_init(void);

/**
 * Periodically trace a message from ZBOSS core to inform external systems (for example, TDF)
 *
 * @param param - unused; it allows to use the ZBOSS scheduler
 *
 * You can redefine the message period in your vendor configuration file, @see ZB_DIAG_CORE_WATCHDOG_TMO_MS
 */
void zb_diag_core_watchdog(zb_uint8_t param);

#endif /* ZB_DIAG_CORE_WATCHDOG_H */
