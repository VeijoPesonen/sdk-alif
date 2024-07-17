/* Copyright (C) 2023 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UNALIGNED_GET
struct __attribute__((packed, aligned(1))) T_UINT32_READ {
	uint32_t v;
};
#define UNALIGNED_GET(addr) (((const struct T_UINT32_READ *)(const void *)(addr))->v)
#endif

#include "mac154app.h"
#include "mac154_err.h"

#include "ahi_msg_lib.h"

/*AHI message definitions*/
#define TL_HEADER_LEN      9
#define TL_AHI_CMD_LEN     80
#define TL_AHI_PAYLOAD_LEN 127
#define TX_BUFFER_LEN      (TL_AHI_PAYLOAD_LEN + TL_HEADER_LEN + TL_AHI_CMD_LEN)
#define AHI_KE_MSG_TYPE    0x10

/*AHI header handling*/
#define MSG_TYPE(p_msg)     (p_msg[0])
#define MSG_LENGTH(p_msg)   ((p_msg[8] << 8) + p_msg[7])
#define MSG_COMMAND(p_msg)  ((p_msg[2] << 8) + p_msg[1])
#define MSG_SRC_TASK(p_msg) ((p_msg[6] << 8) + p_msg[5])
#define MSG_DST_TASK(p_msg) ((p_msg[4] << 8) + p_msg[3])

static enum alif_mac154_status_code alif_ahi_msg_status_convert(uint16_t status)
{
	if (status == MAC154_ERR_NO_ERROR) {
		return ALIF_MAC154_STATUS_OK;
	} else if (status == MAC154_ERR_NO_ANSWER) {
		return ALIF_MAC154_STATUS_NO_ACK;
	} else if (status == MAC154_ERR_RADIO_CHANNEL_IN_USE) {
		return ALIF_MAC154_STATUS_CHANNEL_ACCESS_FAILURE;
	} else if (status == MAC154_ERR_HARDWARE_FAILURE) {
		return ALIF_MAC154_STATUS_HW_FAILED;
	} else if (status == MAC154_ERR_RADIO_ISSUE) {
		return ALIF_MAC154_STATUS_HW_FAILED;
	} else if (status == MAC154_ERR_UNKNOWN_COMMAND) {
		return ALIF_MAC154_STATUS_SW_FAILED;
	} else if (status == MAC154_ERR_SFTWR_FAILURE_TX || status == MAC154_ERR_SFTWR_FAILURE_RX ||
		   status == MAC154_ERR_SFTWR_FAILURE_ED) {
		return ALIF_MAC154_STATUS_INVALID_STATE;
	} else if (status == MAC154_ERR_UNKNOWN_COMMAND) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	return ALIF_MAC154_STATUS_FAILED;
}

int alif_ahi_msg_valid_message(struct msg_buf *p_msg)
{

	if (p_msg->msg_len < TL_HEADER_LEN) {
		return 0;
	}
	if (MSG_TYPE(p_msg->msg) != AHI_KE_MSG_TYPE || MSG_DST_TASK(p_msg->msg) != TASK_ID_AHI ||
	    MSG_SRC_TASK(p_msg->msg) != TASK_ID_MAC154APP) {
		return -1;
	}
	if (MSG_LENGTH(p_msg->msg) + TL_HEADER_LEN >= MAX_MSG_LEN) {
		return -2;
	}
	if (p_msg->msg_len < MSG_LENGTH(p_msg->msg) + TL_HEADER_LEN) {
		return 0;
	}
	return 1;
}

bool alif_ahi_msg_resp_event_recv(struct msg_buf *p_dest_msg, struct msg_buf *p_src_msg)
{

	if (!p_dest_msg) {
		return false;
	}
	if (p_dest_msg->rsp_event && p_dest_msg->rsp_event != MSG_COMMAND(p_src_msg->msg)) {
		return false;
	}
	mac154app_cmd_t *p_cmd = (mac154app_cmd_t *)(p_src_msg->msg + TL_HEADER_LEN);

	if (p_dest_msg->rsp_msg && p_dest_msg->rsp_msg != p_cmd->cmd_code) {
		return false;
	}
	memcpy(p_dest_msg->msg, p_src_msg->msg, p_src_msg->msg_len);
	p_dest_msg->msg_len = p_src_msg->msg_len;
	return true;
}

bool alif_ahi_msg_recv_ind_recv(struct msg_buf *p_msg, uint16_t *p_ctx, int8_t *p_rssi,
				bool *p_frame_pending, uint64_t *p_timestamp, uint8_t *p_len,
				uint8_t **p_data)
{
	if (p_msg->msg_len < TL_HEADER_LEN + sizeof(mac154app_rx_frame_ind_t)) {
		return false;
	}
	if (MSG_TYPE(p_msg->msg) != AHI_KE_MSG_TYPE || MSG_COMMAND(p_msg->msg) != MAC154APP_IND) {
		return false;
	}
	mac154app_rx_frame_ind_t *p_ind = (mac154app_rx_frame_ind_t *)(p_msg->msg + TL_HEADER_LEN);

	if (p_ind->ind_code != MAC154APP_RX_FRAME) {
		return false;
	}
	if (p_ctx) {
		*p_ctx = p_ind->dummy;
	}
	if (p_rssi) {
		*p_rssi = p_ind->rssi;
	}
	if (p_frame_pending) {
		*p_frame_pending = p_ind->frame_pending;
	}
	if (p_timestamp) {
		*p_timestamp = ((uint64_t)p_ind->timestamp_h << 32) + p_ind->timestamp_l;
	}
	if (p_len) {
		*p_len = p_ind->len;
	}
	if (p_data) {
		*p_data = p_ind->data;
	}
	return true;
}

bool alif_ahi_msg_rx_start_end_recv(struct msg_buf *p_msg, uint16_t *p_dummy,
				    enum alif_mac154_status_code *p_status)
{
	if (p_msg->msg_len < TL_HEADER_LEN + sizeof(mac154app_start_rx_cmp_evt_t)) {
		return false;
	}
	if (MSG_TYPE(p_msg->msg) != AHI_KE_MSG_TYPE ||
	    MSG_COMMAND(p_msg->msg) != MAC154APP_CMP_EVT) {
		return false;
	}

	mac154app_start_rx_cmp_evt_t *p_cmd =
		(mac154app_start_rx_cmp_evt_t *)(&p_msg->msg[TL_HEADER_LEN]);
	if (p_cmd->cmd_code != MAC154APP_START_RX) {
		return false;
	}
	if (p_dummy) {
		*p_dummy = p_cmd->dummy;
	}
	if (p_status) {
		*p_status = alif_ahi_msg_status_convert(p_cmd->status);
	}
	return true;
}

bool alif_ahi_msg_rx_stop_end_recv(struct msg_buf *p_msg, uint16_t *p_dummy, uint16_t *p_nreceived,
				   enum alif_mac154_status_code *p_status)
{
	if (p_msg->msg_len < TL_HEADER_LEN + sizeof(mac154app_stop_rx_cmp_evt_t)) {
		return false;
	}
	if (MSG_TYPE(p_msg->msg) != AHI_KE_MSG_TYPE ||
	    MSG_COMMAND(p_msg->msg) != MAC154APP_CMP_EVT) {
		return false;
	}

	mac154app_stop_rx_cmp_evt_t *p_cmd =
		(mac154app_stop_rx_cmp_evt_t *)(&p_msg->msg[TL_HEADER_LEN]);

	if (p_cmd->cmd_code != MAC154APP_STOP_RX) {
		return false;
	}
	if (p_dummy) {
		*p_dummy = p_cmd->dummy;
	}
	if (p_nreceived) {
		*p_nreceived = p_cmd->nreceived;
	}
	if (p_status) {
		*p_status = alif_ahi_msg_status_convert(p_cmd->status);
	}
	return true;
}

bool alif_ahi_msg_reset_recv(struct msg_buf *p_msg, uint16_t *p_dummy, uint8_t *p_activity)
{
	if (p_msg->msg_len < TL_HEADER_LEN + sizeof(mac154app_mm_reset_msg_ind_t)) {
		return false;
	}
	if (MSG_TYPE(p_msg->msg) != AHI_KE_MSG_TYPE || MSG_COMMAND(p_msg->msg) != MAC154APP_IND) {
		return false;
	}

	mac154app_mm_reset_msg_ind_t *p_cmd =
		(mac154app_mm_reset_msg_ind_t *)(&p_msg->msg[TL_HEADER_LEN]);

	if (p_cmd->ind_code != MAC154APP_MM_RESET) {
		return false;
	}
	if (p_dummy) {
		*p_dummy = p_cmd->dummy;
	}
	if (p_activity) {
		*p_activity = p_cmd->activity;
	}
	return true;
}

bool alif_ahi_msg_error_recv(struct msg_buf *p_msg, uint16_t *p_dummy,
			     enum alif_mac154_status_code *p_status)
{
	if (p_msg->msg_len < TL_HEADER_LEN + sizeof(mac154app_error_msg_ind_t)) {
		return false;
	}
	if (MSG_TYPE(p_msg->msg) != AHI_KE_MSG_TYPE || MSG_COMMAND(p_msg->msg) != MAC154APP_IND) {
		return false;
	}

	mac154app_error_msg_ind_t *p_cmd =
		(mac154app_error_msg_ind_t *)(&p_msg->msg[TL_HEADER_LEN]);

	if (p_cmd->ind_code != MAC154APP_ERR_INFO) {
		return false;
	}
	if (p_dummy) {
		*p_dummy = p_cmd->dummy;
	}
	if (p_status) {
		if (p_cmd->err_code == MAC154APP_ERR_HW_OUT_OF_SYNC) {
			*p_status = ALIF_MAC154_STATUS_OUT_OF_SYNC;
		} else {
			*p_status = ALIF_MAC154_STATUS_FAILED;
		}
	}
	return true;
}

static void *alif_ahi_msg_header_validate(struct msg_buf *p_msg, uint16_t cmd, int msg_size)
{
	if (!p_msg || p_msg->msg_len < TL_HEADER_LEN + msg_size) {
		return NULL;
	}
	if (MSG_TYPE(p_msg->msg) != AHI_KE_MSG_TYPE) {
		return NULL;
	}
	if (MSG_COMMAND(p_msg->msg) != cmd || MSG_LENGTH(p_msg->msg) < msg_size) {
		return NULL;
	}
	return &p_msg->msg[TL_HEADER_LEN];
}

static void *alif_ahi_msg_header_write(struct msg_buf *p_msg, uint16_t cmd_length,
				       uint16_t data_length)
{
	p_msg->msg[0] = AHI_KE_MSG_TYPE;

	/* Command ID */
	p_msg->msg[1] = (MAC154APP_CMD & 0xFF);
	p_msg->msg[2] = (MAC154APP_CMD & 0xFF00) >> 8;

	/* Destination task */
	p_msg->msg[3] = (TASK_ID_MAC154APP & 0xFF);
	p_msg->msg[4] = (TASK_ID_MAC154APP & 0xFF00) >> 8;

	/* Source task (for routing response)*/
	p_msg->msg[5] = (TASK_ID_AHI & 0xFF);
	p_msg->msg[6] = (TASK_ID_AHI & 0xFF00) >> 8;

	/* Length of following data */
	p_msg->msg[7] = ((cmd_length + data_length) & 0xFF);
	p_msg->msg[8] = ((cmd_length + data_length) & 0xFF00) >> 8;
	p_msg->msg_len = TL_HEADER_LEN;

	/*update the structure length*/
	p_msg->msg_len += cmd_length + data_length;

	return &p_msg->msg[9];
}

void alif_ahi_msg_pan_id_set(struct msg_buf *p_msg, uint16_t ctx, uint16_t pan_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PAN_ID_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PAN_ID_SET;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = pan_id;
}

void alif_ahi_msg_cca_mode_set(struct msg_buf *p_msg, uint16_t ctx, enum alif_mac154_cca_mode mode)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CCA_MODE_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CCA_MODE_SET;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = mode;
}

void alif_ahi_msg_ed_threshold_set(struct msg_buf *p_msg, uint16_t ctx, int8_t threshold)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_ED_THRESHOLD_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_ED_THRESHOLD_SET;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = ((uint8_t)threshold);
}

void alif_ahi_msg_pan_id_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_id_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PAN_ID_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PAN_ID_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_short_id_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_id_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_SHORT_ID_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_SHORT_ID_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_long_id_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_id_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_LONG_ID_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_LONG_ID_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_cca_mode_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_id_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CCA_MODE_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CCA_MODE_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_ed_threshold_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_id_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_ED_THRESHOLD_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_ED_THRESHOLD_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_short_id_set(struct msg_buf *p_msg, uint16_t ctx, uint16_t short_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_SHORT_ID_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_SHORT_ID_SET;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = short_id;
}

void alif_ahi_msg_pending_short_id_find(struct msg_buf *p_msg, uint16_t ctx, uint16_t short_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PENDINGS_SHORT_ID_FIND;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PENDINGS_SHORT_ID_FIND;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = short_id;
}

void alif_ahi_msg_pending_short_id_insert(struct msg_buf *p_msg, uint16_t ctx, uint16_t short_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PENDINGS_SHORT_ID_INSERT;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PENDINGS_SHORT_ID_INSERT;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = short_id;
}

void alif_ahi_msg_pending_short_id_remove(struct msg_buf *p_msg, uint16_t ctx, uint16_t short_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PENDINGS_SHORT_ID_REMOVE;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PENDINGS_SHORT_ID_REMOVE;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = short_id;
}

void alif_ahi_msg_long_id_set(struct msg_buf *p_msg, uint16_t ctx, uint8_t *p_extended_address)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_LONG_ID_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_LONG_ID_SET;
	p_cmd->dummy = ctx;
	p_cmd->value_h = (p_extended_address[7] << 24) + (p_extended_address[6] << 16) +
			 (p_extended_address[5] << 8) + p_extended_address[4];
	p_cmd->value_l = (p_extended_address[3] << 24) + (p_extended_address[2] << 16) +
			 (p_extended_address[1] << 8) + p_extended_address[0];
}

void alif_ahi_msg_pending_long_id_find(struct msg_buf *p_msg, uint16_t ctx,
				       uint8_t *p_extended_address)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PENDINGS_LONG_ID_FIND;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PENDINGS_LONG_ID_FIND;
	p_cmd->dummy = ctx;
	p_cmd->value_h = (p_extended_address[7] << 24) + (p_extended_address[6] << 16) +
			 (p_extended_address[5] << 8) + p_extended_address[4];
	p_cmd->value_l = (p_extended_address[3] << 24) + (p_extended_address[2] << 16) +
			 (p_extended_address[1] << 8) + p_extended_address[0];
}

void alif_ahi_msg_pending_long_id_insert(struct msg_buf *p_msg, uint16_t ctx,
					 uint8_t *p_extended_address)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PENDINGS_LONG_ID_INSERT;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PENDINGS_LONG_ID_INSERT;
	p_cmd->dummy = ctx;
	p_cmd->value_h = (p_extended_address[7] << 24) + (p_extended_address[6] << 16) +
			 (p_extended_address[5] << 8) + p_extended_address[4];
	p_cmd->value_l = (p_extended_address[3] << 24) + (p_extended_address[2] << 16) +
			 (p_extended_address[1] << 8) + p_extended_address[0];
}

void alif_ahi_msg_pending_long_id_remove(struct msg_buf *p_msg, uint16_t ctx,
					 uint8_t *p_extended_address)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PENDINGS_LONG_ID_REMOVE;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PENDINGS_LONG_ID_REMOVE;
	p_cmd->dummy = ctx;
	p_cmd->value_h = (p_extended_address[7] << 24) + (p_extended_address[6] << 16) +
			 (p_extended_address[5] << 8) + p_extended_address[4];
	p_cmd->value_l = (p_extended_address[3] << 24) + (p_extended_address[2] << 16) +
			 (p_extended_address[1] << 8) + p_extended_address[0];
}

void alif_ahi_msg_timestamp_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_timestamp_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_TIMESTAMP_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_timestamp_get_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_TIMESTAMP_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_rx_stop(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_stop_rx_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_STOP_RX;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_stop_rx_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_STOP_RX;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_promiscuous_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_promiscuous_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PROMISCUOUS_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_promiscuous_get_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PROMISCUOUS_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_promiscuous_set(struct msg_buf *p_msg, uint16_t ctx, uint8_t input)
{
	mac154app_promiscuous_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_PROMISCUOUS_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_promiscuous_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_PROMISCUOUS_SET;
	p_cmd->dummy = ctx;
	p_cmd->input = input;
}

void alif_ahi_msg_tx_power_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_promiscuous_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_TXPOWER_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_promiscuous_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_TXPOWER_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_min_tx_power_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_promiscuous_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_MINTXPOWER_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_promiscuous_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_MINTXPOWER_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_max_tx_power_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_promiscuous_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_MAXTXPOWER_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_promiscuous_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_MAXTXPOWER_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_last_rssi_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_promiscuous_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_LAST_RSSI_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_promiscuous_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_LAST_RSSI_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_max_tx_power_set(struct msg_buf *p_msg, uint16_t ctx, uint8_t dbm)
{
	mac154app_txpower_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_TXPOWER_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_txpower_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_TXPOWER_SET;
	p_cmd->dummy = ctx;
	p_cmd->input_dbm = dbm;
}

void alif_ahi_msg_ed_start(struct msg_buf *p_msg, uint16_t ctx, uint8_t channel, int8_t threshold,
			   uint8_t ticks, uint32_t timestamp)
{
	mac154app_start_ed_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_START_ED;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_start_ed_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_START_ED;
	p_cmd->dummy = ctx;
	p_cmd->channel = channel;
	p_cmd->threshold = threshold;
	p_cmd->nb_tics = ticks;
	p_cmd->timestamp = timestamp;
}

void alif_ahi_msg_reset(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_reset_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_RESET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_reset_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_RESET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_version_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_get_version_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_GET_VERSION;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_get_version_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_GET_VERSION;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_tx_start(struct msg_buf *p_msg, uint16_t ctx, uint8_t channel,
			   uint8_t cca_requested, uint8_t acknowledgment_asked, uint32_t timestamp,
			   const uint8_t *p_data, uint8_t data_len)
{
	mac154app_tx_single_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_TX_SINGLE;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_tx_single_cmd_t), data_len);

	p_cmd->cmd_code = MAC154APP_TX_SINGLE;
	p_cmd->dummy = ctx;
	p_cmd->channel = channel;
	p_cmd->cca_requested = cca_requested;
	p_cmd->acknowledgement_asked = acknowledgment_asked;
	p_cmd->timestamp = timestamp;
	p_cmd->len = data_len;
	memcpy(p_cmd->data, p_data, data_len);
}

void alif_ahi_msg_rx_start(struct msg_buf *p_msg, uint16_t ctx, uint8_t channel,
			   bool mute_indications, uint8_t nb_frames, uint32_t timestamp)
{
	mac154app_start_rx_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_START_RX;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_start_rx_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_START_RX;
	p_cmd->dummy = ctx;
	p_cmd->channel = channel;
	p_cmd->b_mute_indications = mute_indications;
	p_cmd->nb_frames = nb_frames;
	p_cmd->timestamp = timestamp;
}

void alif_ahi_msg_tx_prio_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_prio_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_TX_PRIO_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_prio_get_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_TX_PRIO_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_rx_prio_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_prio_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_RX_PRIO_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_prio_get_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_RX_PRIO_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_ed_prio_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_prio_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_ED_PRIO_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_prio_get_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_ED_PRIO_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_tx_prio_set(struct msg_buf *p_msg, uint16_t ctx, uint8_t priority)
{
	mac154app_prio_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_TX_PRIO_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_prio_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_TX_PRIO_SET;
	p_cmd->dummy = ctx;
	p_cmd->prio = priority;
}

void alif_ahi_msg_rx_prio_set(struct msg_buf *p_msg, uint16_t ctx, uint8_t priority)
{
	mac154app_prio_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_RX_PRIO_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_prio_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_RX_PRIO_SET;
	p_cmd->dummy = ctx;
	p_cmd->prio = priority;
}

void alif_ahi_msg_ed_prio_set(struct msg_buf *p_msg, uint16_t ctx, uint8_t priority)
{
	mac154app_prio_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_ED_PRIO_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_prio_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_ED_PRIO_SET;
	p_cmd->dummy = ctx;
	p_cmd->prio = priority;
}

void alif_ahi_msg_dbg_rf(struct msg_buf *p_msg, uint16_t ctx, uint8_t write, uint32_t address,
			 uint32_t value)
{
	struct mac154app_dbg_rw_rf_cmd *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_DBG_RW_RF;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(struct mac154app_dbg_rw_rf_cmd), 0);

	p_cmd->cmd_code = MAC154APP_DBG_RW_RF;
	p_cmd->dummy = ctx;
	p_cmd->write = write;
	p_cmd->addr = address;
	p_cmd->data = value;
}

/*
 * Generic message parser
 *
 */

enum alif_mac154_status_code alif_ahi_msg_status(struct msg_buf *p_msg, uint8_t *p_ctx)
{
	mac154app_cmp_evt_t *p_cmd_resp;

	p_cmd_resp =
		alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT, sizeof(mac154app_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}

	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	return alif_ahi_msg_status_convert(p_cmd_resp->status);
}

enum alif_mac154_status_code alif_ahi_msg_dbm(struct msg_buf *p_msg, uint8_t *p_ctx, int8_t *p_dbm)
{
	mac154app_dbm_get_cmp_evt_t *p_cmd_resp;
	/*  used for MAC154APP_TXPOWER_GET, MAC154APP_MINTXPOWER_GET, MAC154APP_MAXTXPOWER_GET &
	    MAC154APP_LAST_RSSI_GET */

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_dbm_get_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_dbm) {
		*p_dbm = p_cmd_resp->answer_dbm;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_prio(struct msg_buf *p_msg, uint8_t *p_ctx,
					       int8_t *p_prio)
{
	mac154app_prio_get_cmp_evt_t *p_cmd_resp;
	/*  used for MAC154APP_TX_PRIO_GET, MAC154APP_RX_PRIO_GET, MAC154APP_ED_PRIO_GET */

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_dbm_get_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_prio) {
		*p_prio = p_cmd_resp->prio;
	}
	return ALIF_MAC154_STATUS_OK;
}

/*
 * Complete message reception parsers
 *
 */

enum alif_mac154_status_code alif_ahi_msg_pan_id(struct msg_buf *p_msg, uint8_t *p_ctx,
						 uint8_t *p_pan_id)
{
	mac154app_id_get_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_id_set_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_PAN_ID_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_pan_id) {
		*p_pan_id = p_cmd_resp->value_l;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_short_id(struct msg_buf *p_msg, uint8_t *p_ctx,
						   uint8_t *p_short_id)
{
	mac154app_id_get_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_id_set_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_SHORT_ID_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_short_id) {
		*p_short_id = p_cmd_resp->value_l;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_long_id(struct msg_buf *p_msg, uint8_t *p_ctx,
						  uint8_t *p_extended_address)
{
	mac154app_id_get_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_id_set_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_LONG_ID_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_extended_address) {
		p_extended_address[0] = (p_cmd_resp->value_l) & 0xff;
		p_extended_address[1] = (p_cmd_resp->value_l >> 8) & 0xff;
		p_extended_address[2] = (p_cmd_resp->value_l >> 16) & 0xff;
		p_extended_address[3] = (p_cmd_resp->value_l >> 24) & 0xff;
		p_extended_address[4] = (p_cmd_resp->value_h) & 0xff;
		p_extended_address[5] = (p_cmd_resp->value_h >> 8) & 0xff;
		p_extended_address[6] = (p_cmd_resp->value_h >> 16) & 0xff;
		p_extended_address[7] = (p_cmd_resp->value_h >> 24) & 0xff;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_cca_mode(struct msg_buf *p_msg, uint8_t *p_ctx,
						   uint8_t *p_cca_mode)
{
	mac154app_id_get_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_id_set_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_CCA_MODE_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_cca_mode) {
		*p_cca_mode = p_cmd_resp->value_l;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_ed_threshold(struct msg_buf *p_msg, uint8_t *p_ctx,
						       int8_t *p_ed_threshold)
{
	mac154app_id_get_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_id_set_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_ED_THRESHOLD_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_ed_threshold) {
		*p_ed_threshold = p_cmd_resp->value_l;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_timestamp(struct msg_buf *p_msg, uint8_t *p_ctx,
						    uint64_t *p_timestamp)
{
	mac154app_id_get_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_id_set_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_TIMESTAMP_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_timestamp) {
		*p_timestamp = (((uint64_t)p_cmd_resp->value_h) << 32) + p_cmd_resp->value_l;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_promiscuous_mode(struct msg_buf *p_msg, uint8_t *p_ctx,
							   uint8_t *p_answer)
{
	mac154app_promiscuous_get_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_promiscuous_get_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_PROMISCUOUS_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_answer) {
		*p_answer = p_cmd_resp->answer;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_tx_power_set_status(struct msg_buf *p_msg, uint8_t *p_ctx)
{
	mac154app_txpower_set_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_id_set_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_TXPOWER_SET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}

	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_reset_status(struct msg_buf *p_msg, uint8_t *p_ctx)
{
	mac154app_reset_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_reset_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_RESET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}

	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_version(struct msg_buf *p_msg, uint8_t *p_ctx,
						  uint32_t *p_hw_ver, uint32_t *p_sw_ver)
{
	mac154app_get_version_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_get_version_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_GET_VERSION) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_hw_ver) {
		*p_hw_ver = p_cmd_resp->hw_version;
	}
	if (p_sw_ver) {
		*p_sw_ver = p_cmd_resp->sw_version;
	}

	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_energy_detect_resp(struct msg_buf *p_msg, uint8_t *p_ctx,
							     uint8_t *p_nb_measure,
							     uint8_t *p_average, uint8_t *p_max)
{
	mac154app_start_ed_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_start_ed_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_START_ED) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_nb_measure) {
		*p_nb_measure = p_cmd_resp->nmeasure;
	}
	if (p_average) {
		*p_average = p_cmd_resp->average;
	}
	if (p_max) {
		*p_max = p_cmd_resp->max;
	}
	return ALIF_MAC154_STATUS_OK;
}

/*Older version message structure for backward compability*/
typedef struct mac154app_tx_single_1_0_cmp_evt {
	uint16_t cmd_code;
	uint16_t dummy;
	uint16_t status;
	uint8_t tx_status;
	int8_t ack_rssi;
	uint32_t ack_timestamp_h;
	uint32_t ack_timestamp_l;
	uint32_t ack_msg_begin;
} mac154app_tx_single_1_0_cmp_evt_t;

enum alif_mac154_status_code alif_ahi_msg_tx_start_resp(struct msg_buf *p_msg, uint8_t *p_ctx,
							int8_t *p_rssi, uint64_t *p_timestamp,
							uint8_t *p_ack, uint8_t *p_ack_len)
{
	mac154app_tx_single_1_0_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_tx_single_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_TX_SINGLE) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_rssi) {
		*p_rssi = p_cmd_resp->ack_rssi;
	}
	if (p_timestamp) {
		*p_timestamp = ((uint64_t)UNALIGNED_GET(&p_cmd_resp->ack_timestamp_h) << 32) +
			       UNALIGNED_GET(&p_cmd_resp->ack_timestamp_l);
	}
	if (p_ack_len) {
		*p_ack_len = 3;
	}
	if (p_ack) {
		p_ack[0] = p_cmd_resp->ack_msg_begin >> 8;
		p_ack[1] = p_cmd_resp->ack_msg_begin >> 16;
		p_ack[2] = p_cmd_resp->ack_msg_begin >> 24;
	}

	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_rx_start_resp(struct msg_buf *p_msg, uint8_t *p_ctx)
{
	mac154app_start_rx_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_start_rx_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_START_RX) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	return alif_ahi_msg_status_convert(p_cmd_resp->status);
}

enum alif_mac154_status_code alif_ahi_msg_stop_rx_resp(struct msg_buf *p_msg, uint8_t *p_ctx,
						       uint16_t *p_nreceived)
{
	mac154app_stop_rx_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_stop_rx_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_STOP_RX) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_nreceived) {
		*p_nreceived = p_cmd_resp->nreceived;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_rf_dbg_resp(struct msg_buf *p_msg, uint8_t *p_ctx,
						      uint32_t *p_value)
{
	mac154app_dbg_rw_rf_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_dbg_rw_rf_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_value) {
		*p_value = p_cmd_resp->data;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_tx_start_resp_1_1_0(struct msg_buf *p_msg, uint8_t *p_ctx,
							      int8_t *p_rssi, uint64_t *p_timestamp,
							      uint8_t *p_ack, uint8_t *p_ack_len)
{
	mac154app_tx_single_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_tx_single_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_TX_SINGLE) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_rssi) {
		*p_rssi = p_cmd_resp->ack_rssi;
	}
	if (p_timestamp) {
		*p_timestamp = ((uint64_t)UNALIGNED_GET(&p_cmd_resp->ack_timestamp_h) << 32) +
			       UNALIGNED_GET(&p_cmd_resp->ack_timestamp_l);
	}
	if (p_ack_len) {
		*p_ack_len = p_cmd_resp->length;
	}
	if (p_ack) {
		memcpy(p_ack, p_cmd_resp->ack_msg_begin, p_cmd_resp->length);
	}

	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_mem_dbg_resp(struct msg_buf *p_msg, uint8_t *p_ctx,
						       uint32_t *p_value)
{
	mac154app_dbg_rw_mem_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_dbg_rw_mem_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_value) {
		*p_value = p_cmd_resp->data;
	}
	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_mem_reg_resp(struct msg_buf *p_msg, uint8_t *p_ctx,
						       uint32_t *p_value)
{
	mac154app_dbg_rw_reg_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_dbg_rw_reg_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_value) {
		*p_value = p_cmd_resp->data;
	}
	return ALIF_MAC154_STATUS_OK;
}

void alif_ahi_msg_dbg_mem(struct msg_buf *p_msg, uint16_t ctx, uint8_t write, uint32_t address,
			  uint32_t value)
{
	struct mac154app_dbg_rw_mem_cmd *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_DBG_RW_MEM;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(struct mac154app_dbg_rw_mem_cmd), 0);

	p_cmd->cmd_code = MAC154APP_DBG_RW_MEM;
	p_cmd->dummy = ctx;
	p_cmd->write = write;
	p_cmd->addr = address;
	p_cmd->data = value;
}

void alif_ahi_msg_dbg_reg(struct msg_buf *p_msg, uint16_t ctx, uint8_t write, uint32_t address,
			  uint32_t value)
{
	struct mac154app_dbg_rw_reg_cmd *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_DBG_RW_REG;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(struct mac154app_dbg_rw_reg_cmd), 0);

	p_cmd->cmd_code = MAC154APP_DBG_RW_REG;
	p_cmd->dummy = ctx;
	p_cmd->write = write;
	p_cmd->addr = address;
	p_cmd->data = value;
}

void alif_ahi_msg_csl_long_id_find(struct msg_buf *p_msg, uint16_t ctx, uint8_t *p_extended_address)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_LONG_ID_FIND;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_LONG_ID_FIND;
	p_cmd->dummy = ctx;
	p_cmd->value_h = (p_extended_address[7] << 24) + (p_extended_address[6] << 16) +
			 (p_extended_address[5] << 8) + p_extended_address[4];
	p_cmd->value_l = (p_extended_address[3] << 24) + (p_extended_address[2] << 16) +
			 (p_extended_address[1] << 8) + p_extended_address[0];
}

void alif_ahi_msg_csl_long_id_insert(struct msg_buf *p_msg, uint16_t ctx,
				     uint8_t *p_extended_address)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_LONG_ID_INSERT;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_LONG_ID_INSERT;
	p_cmd->dummy = ctx;
	p_cmd->value_h = (p_extended_address[7] << 24) + (p_extended_address[6] << 16) +
			 (p_extended_address[5] << 8) + p_extended_address[4];
	p_cmd->value_l = (p_extended_address[3] << 24) + (p_extended_address[2] << 16) +
			 (p_extended_address[1] << 8) + p_extended_address[0];
}

void alif_ahi_msg_csl_long_id_remove(struct msg_buf *p_msg, uint16_t ctx,
				     uint8_t *p_extended_address)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_LONG_ID_REMOVE;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_LONG_ID_REMOVE;
	p_cmd->dummy = ctx;
	p_cmd->value_h = (p_extended_address[7] << 24) + (p_extended_address[6] << 16) +
			 (p_extended_address[5] << 8) + p_extended_address[4];
	p_cmd->value_l = (p_extended_address[3] << 24) + (p_extended_address[2] << 16) +
			 (p_extended_address[1] << 8) + p_extended_address[0];
}

void alif_ahi_msg_csl_short_id_find(struct msg_buf *p_msg, uint16_t ctx, uint16_t short_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_SHORT_ID_FIND;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_SHORT_ID_FIND;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = short_id;
}

void alif_ahi_msg_csl_short_id_insert(struct msg_buf *p_msg, uint16_t ctx, uint16_t short_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_SHORT_ID_INSERT;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_SHORT_ID_INSERT;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = short_id;
}

void alif_ahi_msg_csl_short_id_remove(struct msg_buf *p_msg, uint16_t ctx, uint16_t short_id)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_LONG_ID_REMOVE;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_LONG_ID_REMOVE;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = short_id;
}

void alif_ahi_msg_csl_period_set(struct msg_buf *p_msg, uint16_t ctx, uint16_t period)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_PERIOD_SET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_PERIOD_SET;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = period;
}

void alif_ahi_msg_csl_period_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_id_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_PERIOD_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_get_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_PERIOD_GET;
	p_cmd->dummy = ctx;
}

void alif_ahi_msg_config_header_ie_csl_reduced(struct msg_buf *p_msg, uint16_t ctx,
					       uint16_t csl_period, uint16_t csl_phase)
{
	mac154app_config_header_ie_csl_reduced_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CONF_CSL_IE_HEADER_REDUCED;

	p_cmd = alif_ahi_msg_header_write(p_msg,
					  sizeof(mac154app_config_header_ie_csl_reduced_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CONF_CSL_IE_HEADER_REDUCED;
	p_cmd->dummy = ctx;
	p_cmd->csl_period = csl_period;
	p_cmd->csl_phase = csl_phase;
}

void alif_ahi_msg_config_header_ie_csl_full(struct msg_buf *p_msg, uint16_t ctx,
					    uint16_t csl_period, uint16_t csl_phase,
					    uint16_t csl_rendezvous_time)
{
	mac154app_config_header_ie_csl_full_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CONF_CSL_IE_HEADER_FULL;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_config_header_ie_csl_full_cmd_t),
					  0);

	p_cmd->cmd_code = MAC154APP_CONF_CSL_IE_HEADER_FULL;
	p_cmd->dummy = ctx;
	p_cmd->csl_period = csl_period;
	p_cmd->csl_phase = csl_phase;
	p_cmd->csl_rendezvous_time = csl_rendezvous_time;
}

void alif_ahi_msg_config_rx_slot(struct msg_buf *p_msg, uint16_t ctx, uint32_t start,
				 uint16_t duration, uint8_t channel)
{
	mac154app_config_rx_slot_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CONF_RX_SLOT;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_config_rx_slot_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CONF_RX_SLOT;
	p_cmd->dummy = ctx;
	p_cmd->start = start;
	p_cmd->duration = duration;
	p_cmd->channel = channel;
}

void alif_ahi_msg_frame_counter_update(struct msg_buf *p_msg, uint16_t ctx, uint32_t frame_counter)
{
	mac154app_id_set_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_FRAME_COUNTER_UPDATE;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_set_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_FRAME_COUNTER_UPDATE;
	p_cmd->dummy = ctx;
	p_cmd->value_h = 0;
	p_cmd->value_l = frame_counter;
}

void alif_ahi_msg_csl_phase_get(struct msg_buf *p_msg, uint16_t ctx)
{
	mac154app_id_get_cmd_t *p_cmd;

	p_msg->rsp_event = MAC154APP_CMP_EVT;
	p_msg->rsp_msg = MAC154APP_CSL_PHASE_GET;

	p_cmd = alif_ahi_msg_header_write(p_msg, sizeof(mac154app_id_get_cmd_t), 0);

	p_cmd->cmd_code = MAC154APP_CSL_PHASE_GET;
	p_cmd->dummy = ctx;
}

enum alif_mac154_status_code alif_ahi_msg_csl_phase_get_resp(struct msg_buf *p_msg, uint8_t *p_ctx,
							 uint64_t *p_timestamp,
							 uint16_t *p_csl_phase)
{
	mac154app_get_csl_phase_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_get_csl_phase_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_CSL_PHASE_GET) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->status != MAC154_ERR_NO_ERROR) {
		return alif_ahi_msg_status_convert(p_cmd_resp->status);
	}
	if (p_csl_phase) {
		*p_csl_phase = p_cmd_resp->csl_phase;
	}
	if (p_timestamp) {
		*p_timestamp = ((uint64_t)p_cmd_resp->value_h << 32) + p_cmd_resp->value_l;
	}

	return ALIF_MAC154_STATUS_OK;
}

enum alif_mac154_status_code alif_ahi_msg_header_ie_csl_full_resp(struct msg_buf *p_msg,
								  uint8_t *p_ctx)
{
	mac154app_config_header_ie_csl_full_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(
		p_msg, MAC154APP_CMP_EVT, sizeof(mac154app_config_header_ie_csl_full_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_CONF_CSL_IE_HEADER_FULL) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	return alif_ahi_msg_status_convert(p_cmd_resp->status);
}

enum alif_mac154_status_code alif_ahi_msg_header_ie_csl_reduced_resp(struct msg_buf *p_msg,
								     uint8_t *p_ctx)
{
	mac154app_config_header_ie_csl_reduced_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(
		p_msg, MAC154APP_CMP_EVT, sizeof(mac154app_config_header_ie_csl_reduced_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_CONF_CSL_IE_HEADER_REDUCED) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	return alif_ahi_msg_status_convert(p_cmd_resp->status);
}

enum alif_mac154_status_code alif_ahi_msg_config_rx_slot_resp(struct msg_buf *p_msg, uint8_t *p_ctx)
{
	mac154app_config_rx_slot_cmp_evt_t *p_cmd_resp;

	p_cmd_resp = alif_ahi_msg_header_validate(p_msg, MAC154APP_CMP_EVT,
						  sizeof(mac154app_config_rx_slot_cmp_evt_t));

	if (!p_cmd_resp) {
		return ALIF_MAC154_STATUS_COMM_FAILURE;
	}
	if (p_ctx) {
		*p_ctx = p_cmd_resp->dummy;
	}
	if (p_cmd_resp->cmd_code != MAC154APP_CONF_RX_SLOT) {
		return ALIF_MAC154_STATUS_INVALID_MESSAGE;
	}
	return alif_ahi_msg_status_convert(p_cmd_resp->status);
}

