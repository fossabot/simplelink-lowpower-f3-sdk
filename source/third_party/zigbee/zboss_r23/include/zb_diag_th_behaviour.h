
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

#ifndef ZB_DIAG_TH_BEHAVIOUR_H
#define ZB_DIAG_TH_BEHAVIOUR_H

/** Empty init-function
 *
 * Should be called from @see zb_diag_init()
 */
void zb_diag_th_behaviour_init(void);

zb_bool_t zb_diag_th_is_disable_support_kn_tlv_in_beacon(void);
zb_bool_t zb_diag_th_is_disable_update_hub_connectivity(void);

zb_bool_t zb_diag_th_is_keep_provisional_key_for_16_3(void);

#if defined ZB_COORDINATOR_ROLE || defined ZB_ROUTER_ROLE
typedef struct zb_zdo_secur_start_key_negotiation_rsp_send_param_s zb_zdo_secur_start_key_negotiation_rsp_send_param_t;
void zb_diag_th_key_neg_rsp_not_autho(zb_zdo_secur_start_key_negotiation_rsp_send_param_t *resp);
void zb_diag_th_key_neg_rsp_truncated_tlv(zb_bufid_t bufid, zb_secur_ecdhe_common_ctx_t *key_neg_ctx_ptr);
#endif /* ZB_COORDINATOR_ROLE || ZB_ROUTER_ROLE */

zb_bool_t zb_diag_th_is_key_neg_rsp_no_tlv(void);
void zb_diag_th_key_neg_rsp_add_extra_tlv(zb_bufid_t bufid, zb_uint8_t allocation_bytes);

void zb_diag_th_key_neg_req_truncated_tlv(zb_bufid_t bufid);
zb_bool_t zb_diag_th_is_key_neg_req_no_tlv(void);
void zb_diag_th_key_neg_req_add_extra_tlv(zb_bufid_t bufid, zb_uint8_t allocation_bytes);

zb_bool_t zb_diag_th_is_allow_entry_for_unregistered_ep(void);
zb_bool_t zb_diag_th_is_aps_drop_ack(void);
typedef struct zb_tlv_hdr_s zb_tlv_hdr_t;
void zb_diag_th_aps_dup_relayed_frames(zb_tlv_hdr_t *relay_tlv_hdr);
zb_bool_t zb_diag_th_is_disable_aps_sec_for_all_zdo_cmd(void);
zb_bool_t zb_diag_th_is_disable_link_power_negotiation(void);
zb_bool_t zb_diag_th_is_disable_rejoin_resp_timeout(void);
zb_bool_t zb_diag_th_is_disable_tp_aps_encryption(void);
zb_bool_t zb_diag_th_is_force_secure_rejoin(void);
typedef struct zb_mac_beacon_payload_s zb_mac_beacon_payload_t;
void zb_diag_th_reset_beacon_end_device_capacity(zb_mac_beacon_payload_t *ptr);
zb_bool_t zb_diag_th_is_send_rejoin_rsp_wo_secur(void);
zb_bool_t zb_diag_th_is_tc_rejoin_aps_decrypt_error(void);
typedef struct zb_nlme_join_request_s zb_nlme_join_request_t;
void zb_diag_th_tc_rejoin_mac_cap_wrong_rx_on_when_idle(zb_nlme_join_request_t *req);

void zb_diag_th_challenge_req_add_extra_tlv(zb_bufid_t param);
void zb_diag_th_challenge_rsp_wrong_mic(zb_uint8_t *ptr);
zb_bool_t zb_diag_th_is_disable_tp_aps_ack(void);
void zb_diag_th_secur_aps_counter_hack_cb(zb_uint32_t *out_sec_counter);

zb_bool_t zb_diag_th_is_nwk_disable_passive_acks(void);

#endif /* ZB_DIAG_TH_BEHAVIOUR_H */
