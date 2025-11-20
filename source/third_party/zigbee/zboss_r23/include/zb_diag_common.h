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
/* PURPOSE: common functionality of the diagnostic library used in submodules
*/

#ifndef ZB_DIAG_COMMON_H
#define ZB_DIAG_COMMON_H

/**
 * Common function to initialize diagnostic functionality
 *
 * The diagnostic library is compiled in two versions:
 * a) empty library - contains stubs only
 * b) common library - contains all function implementations as is
 *
 * By default ZBOSS libs are compiled with the 1st type of the diagnostic lib,
 * so we have two ways to build an app:
 * 1) link ZBOSS libs only - diagnostic functionality will be DISABLED
 * 2) link the common diagnostic lib before ZBOSS libs - diagnostic functionality will be ENABLED
 *
 * To build an app with the diagnostic functionality enabled
 * 1) Need to link the common diagnostic library before ZBOSS libs.
 * 2) Need to call @ref zb_diag_init() function from an application,
 * This will be done automatically from @ref ZB_INIT(). This function
 * should call an init-function for each diagnostic feature. A linker will have
 * to search for zb_diag_init() function and other init-functions in the diagnostic library first.
 *
 * If the diagnostic library with common function implementations is not linked,
 * the the linker will search for init-functions in ZBOSS libs, that contains stubs by default
 */
void zb_diag_init(void);

/* to avoid MISRA violations when a macro expands to ZB_TRUE or ZB_FALSE */
static inline zb_bool_t zb_return_true_func(void) { return ZB_TRUE; }
static inline zb_bool_t zb_return_false_func(void) { return ZB_FALSE; }

#endif /* ZB_DIAG_COMMON_H */
