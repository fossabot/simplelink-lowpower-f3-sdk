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
/*  PURPOSE: NOT standard feature: allow factory new devices join using pre-installed network parameters
*/

#ifndef ZB_BDB_PREINST_NWK_COMMISSIONING_H
#define ZB_BDB_PREINST_NWK_COMMISSIONING_H 1

#ifdef ZB_BDB_PREINST_NWK_JOINING

#ifdef ZB_JOIN_CLIENT

typedef struct zb_bdb_preinst_nwk_parameters_s
{
  zb_uint16_t     panid;
  zb_ext_pan_id_t extpanid;
  zb_uint8_t      nwkkey[ZB_CCM_KEY_SIZE];
  zb_uint32_t     nwkkey_seq_num;
  zb_uint8_t      channel_page;
  zb_uint8_t      channel_number;
  zb_uint16_t     mac_short_address; // If set to 0xFFFF, then random address will be used
  zb_ieee_addr_t  tc_long_address; // If set to 0x00...00 it will not be set.
  zb_uint8_t      tclk[ZB_CCM_KEY_SIZE]; // If set to 0x00...00 it will not be set.
} zb_bdb_preinst_nwk_parameters_t;

/**
 * @brief Set pre-installed joiner init nwk parameters. Mandatory for joining device using pre-installed network.
 *
 * @param zb_bdb_preinst_nwk_parameters_t - Network parameters to be set for joining pre-installed network.
 */
void zb_preinst_joiner_init_nwk_parameters(zb_bdb_preinst_nwk_parameters_t nwk_params);

#endif /* ZB_JOIN_CLIENT */

#ifdef ZB_COORDINATOR_ROLE

/**
 * @brief Set pre-installed joiner's TCLK. For testing purposes only.
 *
 * @param joiner_address - long address of a joiner device.
 * @param tclk - TCLK of the joiner.
 */
void zb_bdb_preinst_nwk_set_joiner_tclk(const zb_ieee_addr_t joiner_address, const zb_uint8_t tclk[ZB_CCM_KEY_SIZE]);

#endif /* ZB_COORDINATOR_ROLE */

/*****************************************************************************/
#endif /* ZB_BDB_PREINST_NWK_JOINING */
#endif /* ZB_BDB_PREINST_NWK_COMMISSIONING_H */

