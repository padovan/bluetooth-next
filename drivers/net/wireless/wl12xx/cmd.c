/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/slab.h>

#include "wl12xx.h"
#include "reg.h"
#include "io.h"
#include "acx.h"
#include "wl12xx_80211.h"
#include "cmd.h"
#include "event.h"
#include "tx.h"

#define WL1271_CMD_FAST_POLL_COUNT       50

/*
 * send command to firmware
 *
 * @wl: wl struct
 * @id: command id
 * @buf: buffer containing the command, must work with dma
 * @len: length of the buffer
 */
int wl1271_cmd_send(struct wl1271 *wl, u16 id, void *buf, size_t len,
		    size_t res_len)
{
	struct wl1271_cmd_header *cmd;
	unsigned long timeout;
	u32 intr;
	int ret = 0;
	u16 status;
	u16 poll_count = 0;

	cmd = buf;
	cmd->id = cpu_to_le16(id);
	cmd->status = 0;

	WARN_ON(len % 4 != 0);
	WARN_ON(test_bit(WL1271_FLAG_IN_ELP, &wl->flags));

	wl1271_write(wl, wl->cmd_box_addr, buf, len, false);

	wl1271_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_CMD);

	timeout = jiffies + msecs_to_jiffies(WL1271_COMMAND_TIMEOUT);

	intr = wl1271_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	while (!(intr & WL1271_ACX_INTR_CMD_COMPLETE)) {
		if (time_after(jiffies, timeout)) {
			wl1271_error("command complete timeout");
			ret = -ETIMEDOUT;
			goto fail;
		}

		poll_count++;
		if (poll_count < WL1271_CMD_FAST_POLL_COUNT)
			udelay(10);
		else
			msleep(1);

		intr = wl1271_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	}

	/* read back the status code of the command */
	if (res_len == 0)
		res_len = sizeof(struct wl1271_cmd_header);
	wl1271_read(wl, wl->cmd_box_addr, cmd, res_len, false);

	status = le16_to_cpu(cmd->status);
	if (status != CMD_STATUS_SUCCESS) {
		wl1271_error("command execute failure %d", status);
		ret = -EIO;
		goto fail;
	}

	wl1271_write32(wl, ACX_REG_INTERRUPT_ACK,
		       WL1271_ACX_INTR_CMD_COMPLETE);
	return 0;

fail:
	WARN_ON(1);
	wl12xx_queue_recovery_work(wl);
	return ret;
}

int wl1271_cmd_general_parms(struct wl1271 *wl)
{
	struct wl1271_general_parms_cmd *gen_parms;
	struct wl1271_ini_general_params *gp =
		&((struct wl1271_nvs_file *)wl->nvs)->general_params;
	bool answer = false;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	gen_parms = kzalloc(sizeof(*gen_parms), GFP_KERNEL);
	if (!gen_parms)
		return -ENOMEM;

	gen_parms->test.id = TEST_CMD_INI_FILE_GENERAL_PARAM;

	memcpy(&gen_parms->general_params, gp, sizeof(*gp));

	if (gp->tx_bip_fem_auto_detect)
		answer = true;

	/* Override the REF CLK from the NVS with the one from platform data */
	gen_parms->general_params.ref_clock = wl->ref_clock;

	/* LPD mode enable (bits 6-7) in WL1271 AP mode only */
	if (wl->quirks & WL12XX_QUIRK_LPD_MODE)
		gen_parms->general_params.general_settings |=
			GENERAL_SETTINGS_DRPW_LPD;

	ret = wl1271_cmd_test(wl, gen_parms, sizeof(*gen_parms), answer);
	if (ret < 0) {
		wl1271_warning("CMD_INI_FILE_GENERAL_PARAM failed");
		goto out;
	}

	gp->tx_bip_fem_manufacturer =
		gen_parms->general_params.tx_bip_fem_manufacturer;

	wl1271_debug(DEBUG_CMD, "FEM autodetect: %s, manufacturer: %d\n",
		     answer ? "auto" : "manual", gp->tx_bip_fem_manufacturer);

out:
	kfree(gen_parms);
	return ret;
}

int wl128x_cmd_general_parms(struct wl1271 *wl)
{
	struct wl128x_general_parms_cmd *gen_parms;
	struct wl128x_ini_general_params *gp =
		&((struct wl128x_nvs_file *)wl->nvs)->general_params;
	bool answer = false;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	gen_parms = kzalloc(sizeof(*gen_parms), GFP_KERNEL);
	if (!gen_parms)
		return -ENOMEM;

	gen_parms->test.id = TEST_CMD_INI_FILE_GENERAL_PARAM;

	memcpy(&gen_parms->general_params, gp, sizeof(*gp));

	if (gp->tx_bip_fem_auto_detect)
		answer = true;

	/* Replace REF and TCXO CLKs with the ones from platform data */
	gen_parms->general_params.ref_clock = wl->ref_clock;
	gen_parms->general_params.tcxo_ref_clock = wl->tcxo_clock;

	ret = wl1271_cmd_test(wl, gen_parms, sizeof(*gen_parms), answer);
	if (ret < 0) {
		wl1271_warning("CMD_INI_FILE_GENERAL_PARAM failed");
		goto out;
	}

	gp->tx_bip_fem_manufacturer =
		gen_parms->general_params.tx_bip_fem_manufacturer;

	wl1271_debug(DEBUG_CMD, "FEM autodetect: %s, manufacturer: %d\n",
		     answer ? "auto" : "manual", gp->tx_bip_fem_manufacturer);

out:
	kfree(gen_parms);
	return ret;
}

int wl1271_cmd_radio_parms(struct wl1271 *wl)
{
	struct wl1271_nvs_file *nvs = (struct wl1271_nvs_file *)wl->nvs;
	struct wl1271_radio_parms_cmd *radio_parms;
	struct wl1271_ini_general_params *gp = &nvs->general_params;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	radio_parms = kzalloc(sizeof(*radio_parms), GFP_KERNEL);
	if (!radio_parms)
		return -ENOMEM;

	radio_parms->test.id = TEST_CMD_INI_FILE_RADIO_PARAM;

	/* 2.4GHz parameters */
	memcpy(&radio_parms->static_params_2, &nvs->stat_radio_params_2,
	       sizeof(struct wl1271_ini_band_params_2));
	memcpy(&radio_parms->dyn_params_2,
	       &nvs->dyn_radio_params_2[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl1271_ini_fem_params_2));

	/* 5GHz parameters */
	memcpy(&radio_parms->static_params_5,
	       &nvs->stat_radio_params_5,
	       sizeof(struct wl1271_ini_band_params_5));
	memcpy(&radio_parms->dyn_params_5,
	       &nvs->dyn_radio_params_5[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl1271_ini_fem_params_5));

	wl1271_dump(DEBUG_CMD, "TEST_CMD_INI_FILE_RADIO_PARAM: ",
		    radio_parms, sizeof(*radio_parms));

	ret = wl1271_cmd_test(wl, radio_parms, sizeof(*radio_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_RADIO_PARAM failed");

	kfree(radio_parms);
	return ret;
}

int wl128x_cmd_radio_parms(struct wl1271 *wl)
{
	struct wl128x_nvs_file *nvs = (struct wl128x_nvs_file *)wl->nvs;
	struct wl128x_radio_parms_cmd *radio_parms;
	struct wl128x_ini_general_params *gp = &nvs->general_params;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	radio_parms = kzalloc(sizeof(*radio_parms), GFP_KERNEL);
	if (!radio_parms)
		return -ENOMEM;

	radio_parms->test.id = TEST_CMD_INI_FILE_RADIO_PARAM;

	/* 2.4GHz parameters */
	memcpy(&radio_parms->static_params_2, &nvs->stat_radio_params_2,
	       sizeof(struct wl128x_ini_band_params_2));
	memcpy(&radio_parms->dyn_params_2,
	       &nvs->dyn_radio_params_2[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl128x_ini_fem_params_2));

	/* 5GHz parameters */
	memcpy(&radio_parms->static_params_5,
	       &nvs->stat_radio_params_5,
	       sizeof(struct wl128x_ini_band_params_5));
	memcpy(&radio_parms->dyn_params_5,
	       &nvs->dyn_radio_params_5[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl128x_ini_fem_params_5));

	radio_parms->fem_vendor_and_options = nvs->fem_vendor_and_options;

	wl1271_dump(DEBUG_CMD, "TEST_CMD_INI_FILE_RADIO_PARAM: ",
		    radio_parms, sizeof(*radio_parms));

	ret = wl1271_cmd_test(wl, radio_parms, sizeof(*radio_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_RADIO_PARAM failed");

	kfree(radio_parms);
	return ret;
}

int wl1271_cmd_ext_radio_parms(struct wl1271 *wl)
{
	struct wl1271_ext_radio_parms_cmd *ext_radio_parms;
	struct conf_rf_settings *rf = &wl->conf.rf;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	ext_radio_parms = kzalloc(sizeof(*ext_radio_parms), GFP_KERNEL);
	if (!ext_radio_parms)
		return -ENOMEM;

	ext_radio_parms->test.id = TEST_CMD_INI_FILE_RF_EXTENDED_PARAM;

	memcpy(ext_radio_parms->tx_per_channel_power_compensation_2,
	       rf->tx_per_channel_power_compensation_2,
	       CONF_TX_PWR_COMPENSATION_LEN_2);
	memcpy(ext_radio_parms->tx_per_channel_power_compensation_5,
	       rf->tx_per_channel_power_compensation_5,
	       CONF_TX_PWR_COMPENSATION_LEN_5);

	wl1271_dump(DEBUG_CMD, "TEST_CMD_INI_FILE_EXT_RADIO_PARAM: ",
		    ext_radio_parms, sizeof(*ext_radio_parms));

	ret = wl1271_cmd_test(wl, ext_radio_parms, sizeof(*ext_radio_parms), 0);
	if (ret < 0)
		wl1271_warning("TEST_CMD_INI_FILE_RF_EXTENDED_PARAM failed");

	kfree(ext_radio_parms);
	return ret;
}

/*
 * Poll the mailbox event field until any of the bits in the mask is set or a
 * timeout occurs (WL1271_EVENT_TIMEOUT in msecs)
 */
static int wl1271_cmd_wait_for_event_or_timeout(struct wl1271 *wl, u32 mask)
{
	u32 events_vector, event;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(WL1271_EVENT_TIMEOUT);

	do {
		if (time_after(jiffies, timeout)) {
			wl1271_debug(DEBUG_CMD, "timeout waiting for event %d",
				     (int)mask);
			return -ETIMEDOUT;
		}

		msleep(1);

		/* read from both event fields */
		wl1271_read(wl, wl->mbox_ptr[0], &events_vector,
			    sizeof(events_vector), false);
		event = events_vector & mask;
		wl1271_read(wl, wl->mbox_ptr[1], &events_vector,
			    sizeof(events_vector), false);
		event |= events_vector & mask;
	} while (!event);

	return 0;
}

static int wl1271_cmd_wait_for_event(struct wl1271 *wl, u32 mask)
{
	int ret;

	ret = wl1271_cmd_wait_for_event_or_timeout(wl, mask);
	if (ret != 0) {
		wl12xx_queue_recovery_work(wl);
		return ret;
	}

	return 0;
}

int wl12xx_cmd_role_enable(struct wl1271 *wl, u8 role_type, u8 *role_id)
{
	struct wl12xx_cmd_role_enable *cmd;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd role enable");

	if (WARN_ON(*role_id != WL12XX_INVALID_ROLE_ID))
		return -EBUSY;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	/* get role id */
	cmd->role_id = find_first_zero_bit(wl->roles_map, WL12XX_MAX_ROLES);
	if (cmd->role_id >= WL12XX_MAX_ROLES) {
		ret = -EBUSY;
		goto out_free;
	}

	memcpy(cmd->mac_address, wl->mac_addr, ETH_ALEN);
	cmd->role_type = role_type;

	ret = wl1271_cmd_send(wl, CMD_ROLE_ENABLE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role enable");
		goto out_free;
	}

	__set_bit(cmd->role_id, wl->roles_map);
	*role_id = cmd->role_id;

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_disable(struct wl1271 *wl, u8 *role_id)
{
	struct wl12xx_cmd_role_disable *cmd;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd role disable");

	if (WARN_ON(*role_id == WL12XX_INVALID_ROLE_ID))
		return -ENOENT;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}
	cmd->role_id = *role_id;

	ret = wl1271_cmd_send(wl, CMD_ROLE_DISABLE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role disable");
		goto out_free;
	}

	__clear_bit(*role_id, wl->roles_map);
	*role_id = WL12XX_INVALID_ROLE_ID;

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wl12xx_allocate_link(struct wl1271 *wl, u8 *hlid)
{
	u8 link = find_first_zero_bit(wl->links_map, WL12XX_MAX_LINKS);
	if (link >= WL12XX_MAX_LINKS)
		return -EBUSY;

	__set_bit(link, wl->links_map);
	*hlid = link;
	return 0;
}

static void wl12xx_free_link(struct wl1271 *wl, u8 *hlid)
{
	if (*hlid == WL12XX_INVALID_LINK_ID)
		return;

	__clear_bit(*hlid, wl->links_map);
	*hlid = WL12XX_INVALID_LINK_ID;
}

static int wl12xx_get_new_session_id(struct wl1271 *wl)
{
	if (wl->session_counter >= SESSION_COUNTER_MAX)
		wl->session_counter = 0;

	wl->session_counter++;

	return wl->session_counter;
}

int wl12xx_cmd_role_start_dev(struct wl1271 *wl)
{
	struct wl12xx_cmd_role_start *cmd;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role start dev %d", wl->dev_role_id);

	cmd->role_id = wl->dev_role_id;
	if (wl->band == IEEE80211_BAND_5GHZ)
		cmd->band = WL12XX_BAND_5GHZ;
	cmd->channel = wl->channel;

	if (wl->dev_hlid == WL12XX_INVALID_LINK_ID) {
		ret = wl12xx_allocate_link(wl, &wl->dev_hlid);
		if (ret)
			goto out_free;
	}
	cmd->device.hlid = wl->dev_hlid;
	cmd->device.session = wl->session_counter;

	wl1271_debug(DEBUG_CMD, "role start: roleid=%d, hlid=%d, session=%d",
		     cmd->role_id, cmd->device.hlid, cmd->device.session);

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role enable");
		goto err_hlid;
	}

	goto out_free;

err_hlid:
	/* clear links on error */
	__clear_bit(wl->dev_hlid, wl->links_map);
	wl->dev_hlid = WL12XX_INVALID_LINK_ID;


out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_stop_dev(struct wl1271 *wl)
{
	struct wl12xx_cmd_role_stop *cmd;
	int ret;

	if (WARN_ON(wl->dev_hlid == WL12XX_INVALID_LINK_ID))
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role stop dev");

	cmd->role_id = wl->dev_role_id;
	cmd->disc_type = DISCONNECT_IMMEDIATE;
	cmd->reason = cpu_to_le16(WLAN_REASON_UNSPECIFIED);

	ret = wl1271_cmd_send(wl, CMD_ROLE_STOP, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role stop");
		goto out_free;
	}

	ret = wl1271_cmd_wait_for_event(wl, DISCONNECT_EVENT_COMPLETE_ID);
	if (ret < 0) {
		wl1271_error("cmd role stop dev event completion error");
		goto out_free;
	}

	wl12xx_free_link(wl, &wl->dev_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_start_sta(struct wl1271 *wl)
{
	struct wl12xx_cmd_role_start *cmd;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role start sta %d", wl->role_id);

	cmd->role_id = wl->role_id;
	if (wl->band == IEEE80211_BAND_5GHZ)
		cmd->band = WL12XX_BAND_5GHZ;
	cmd->channel = wl->channel;
	cmd->sta.basic_rate_set = cpu_to_le32(wl->basic_rate_set);
	cmd->sta.beacon_interval = cpu_to_le16(wl->beacon_int);
	cmd->sta.ssid_type = WL12XX_SSID_TYPE_ANY;
	cmd->sta.ssid_len = wl->ssid_len;
	memcpy(cmd->sta.ssid, wl->ssid, wl->ssid_len);
	memcpy(cmd->sta.bssid, wl->bssid, ETH_ALEN);
	cmd->sta.local_rates = cpu_to_le32(wl->rate_set);

	if (wl->sta_hlid == WL12XX_INVALID_LINK_ID) {
		ret = wl12xx_allocate_link(wl, &wl->sta_hlid);
		if (ret)
			goto out_free;
	}
	cmd->sta.hlid = wl->sta_hlid;
	cmd->sta.session = wl12xx_get_new_session_id(wl);
	cmd->sta.remote_rates = cpu_to_le32(wl->rate_set);

	wl1271_debug(DEBUG_CMD, "role start: roleid=%d, hlid=%d, session=%d "
		     "basic_rate_set: 0x%x, remote_rates: 0x%x",
		     wl->role_id, cmd->sta.hlid, cmd->sta.session,
		     wl->basic_rate_set, wl->rate_set);

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role start sta");
		goto err_hlid;
	}

	goto out_free;

err_hlid:
	/* clear links on error. */
	wl12xx_free_link(wl, &wl->sta_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

/* use this function to stop ibss as well */
int wl12xx_cmd_role_stop_sta(struct wl1271 *wl)
{
	struct wl12xx_cmd_role_stop *cmd;
	int ret;

	if (WARN_ON(wl->sta_hlid == WL12XX_INVALID_LINK_ID))
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role stop sta %d", wl->role_id);

	cmd->role_id = wl->role_id;
	cmd->disc_type = DISCONNECT_IMMEDIATE;
	cmd->reason = cpu_to_le16(WLAN_REASON_UNSPECIFIED);

	ret = wl1271_cmd_send(wl, CMD_ROLE_STOP, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role stop sta");
		goto out_free;
	}

	wl12xx_free_link(wl, &wl->sta_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_start_ap(struct wl1271 *wl)
{
	struct wl12xx_cmd_role_start *cmd;
	struct ieee80211_bss_conf *bss_conf = &wl->vif->bss_conf;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd role start ap %d", wl->role_id);

	/*
	 * We currently do not support hidden SSID. The real SSID
	 * should be fetched from mac80211 first.
	 */
	if (wl->ssid_len == 0) {
		wl1271_warning("Hidden SSID currently not supported for AP");
		ret = -EINVAL;
		goto out;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl12xx_allocate_link(wl, &wl->ap_global_hlid);
	if (ret < 0)
		goto out_free;

	ret = wl12xx_allocate_link(wl, &wl->ap_bcast_hlid);
	if (ret < 0)
		goto out_free_global;

	cmd->role_id = wl->role_id;
	cmd->ap.aging_period = cpu_to_le16(wl->conf.tx.ap_aging_period);
	cmd->ap.bss_index = WL1271_AP_BSS_INDEX;
	cmd->ap.global_hlid = wl->ap_global_hlid;
	cmd->ap.broadcast_hlid = wl->ap_bcast_hlid;
	cmd->ap.basic_rate_set = cpu_to_le32(wl->basic_rate_set);
	cmd->ap.beacon_interval = cpu_to_le16(wl->beacon_int);
	cmd->ap.dtim_interval = bss_conf->dtim_period;
	cmd->ap.beacon_expiry = WL1271_AP_DEF_BEACON_EXP;
	cmd->channel = wl->channel;
	cmd->ap.ssid_len = wl->ssid_len;
	cmd->ap.ssid_type = WL12XX_SSID_TYPE_PUBLIC;
	memcpy(cmd->ap.ssid, wl->ssid, wl->ssid_len);
	cmd->ap.local_rates = cpu_to_le32(0xffffffff);

	switch (wl->band) {
	case IEEE80211_BAND_2GHZ:
		cmd->band = RADIO_BAND_2_4GHZ;
		break;
	case IEEE80211_BAND_5GHZ:
		cmd->band = RADIO_BAND_5GHZ;
		break;
	default:
		wl1271_warning("ap start - unknown band: %d", (int)wl->band);
		cmd->band = RADIO_BAND_2_4GHZ;
		break;
	}

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role start ap");
		goto out_free_bcast;
	}

	goto out_free;

out_free_bcast:
	wl12xx_free_link(wl, &wl->ap_bcast_hlid);

out_free_global:
	wl12xx_free_link(wl, &wl->ap_global_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_stop_ap(struct wl1271 *wl)
{
	struct wl12xx_cmd_role_stop *cmd;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role stop ap %d", wl->role_id);

	cmd->role_id = wl->role_id;

	ret = wl1271_cmd_send(wl, CMD_ROLE_STOP, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role stop ap");
		goto out_free;
	}

	wl12xx_free_link(wl, &wl->ap_bcast_hlid);
	wl12xx_free_link(wl, &wl->ap_global_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_role_start_ibss(struct wl1271 *wl)
{
	struct wl12xx_cmd_role_start *cmd;
	struct ieee80211_bss_conf *bss_conf = &wl->vif->bss_conf;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd role start ibss %d", wl->role_id);

	cmd->role_id = wl->role_id;
	if (wl->band == IEEE80211_BAND_5GHZ)
		cmd->band = WL12XX_BAND_5GHZ;
	cmd->channel = wl->channel;
	cmd->ibss.basic_rate_set = cpu_to_le32(wl->basic_rate_set);
	cmd->ibss.beacon_interval = cpu_to_le16(wl->beacon_int);
	cmd->ibss.dtim_interval = bss_conf->dtim_period;
	cmd->ibss.ssid_type = WL12XX_SSID_TYPE_ANY;
	cmd->ibss.ssid_len = wl->ssid_len;
	memcpy(cmd->ibss.ssid, wl->ssid, wl->ssid_len);
	memcpy(cmd->ibss.bssid, wl->bssid, ETH_ALEN);
	cmd->sta.local_rates = cpu_to_le32(wl->rate_set);

	if (wl->sta_hlid == WL12XX_INVALID_LINK_ID) {
		ret = wl12xx_allocate_link(wl, &wl->sta_hlid);
		if (ret)
			goto out_free;
	}
	cmd->ibss.hlid = wl->sta_hlid;
	cmd->ibss.remote_rates = cpu_to_le32(wl->rate_set);

	wl1271_debug(DEBUG_CMD, "role start: roleid=%d, hlid=%d, session=%d "
		     "basic_rate_set: 0x%x, remote_rates: 0x%x",
		     wl->role_id, cmd->sta.hlid, cmd->sta.session,
		     wl->basic_rate_set, wl->rate_set);

	wl1271_debug(DEBUG_CMD, "wl->bssid = %pM", wl->bssid);

	ret = wl1271_cmd_send(wl, CMD_ROLE_START, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd role enable");
		goto err_hlid;
	}

	goto out_free;

err_hlid:
	/* clear links on error. */
	wl12xx_free_link(wl, &wl->sta_hlid);

out_free:
	kfree(cmd);

out:
	return ret;
}


/**
 * send test command to firmware
 *
 * @wl: wl struct
 * @buf: buffer containing the command, with all headers, must work with dma
 * @len: length of the buffer
 * @answer: is answer needed
 */
int wl1271_cmd_test(struct wl1271 *wl, void *buf, size_t buf_len, u8 answer)
{
	int ret;
	size_t res_len = 0;

	wl1271_debug(DEBUG_CMD, "cmd test");

	if (answer)
		res_len = buf_len;

	ret = wl1271_cmd_send(wl, CMD_TEST, buf, buf_len, res_len);

	if (ret < 0) {
		wl1271_warning("TEST command failed");
		return ret;
	}

	return ret;
}

/**
 * read acx from firmware
 *
 * @wl: wl struct
 * @id: acx id
 * @buf: buffer for the response, including all headers, must work with dma
 * @len: length of buf
 */
int wl1271_cmd_interrogate(struct wl1271 *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd interrogate");

	acx->id = cpu_to_le16(id);

	/* payload length, does not include any headers */
	acx->len = cpu_to_le16(len - sizeof(*acx));

	ret = wl1271_cmd_send(wl, CMD_INTERROGATE, acx, sizeof(*acx), len);
	if (ret < 0)
		wl1271_error("INTERROGATE command failed");

	return ret;
}

/**
 * write acx value to firmware
 *
 * @wl: wl struct
 * @id: acx id
 * @buf: buffer containing acx, including all headers, must work with dma
 * @len: length of buf
 */
int wl1271_cmd_configure(struct wl1271 *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd configure (%d)", id);

	acx->id = cpu_to_le16(id);

	/* payload length, does not include any headers */
	acx->len = cpu_to_le16(len - sizeof(*acx));

	ret = wl1271_cmd_send(wl, CMD_CONFIGURE, acx, len, 0);
	if (ret < 0) {
		wl1271_warning("CONFIGURE command NOK");
		return ret;
	}

	return 0;
}

int wl1271_cmd_data_path(struct wl1271 *wl, bool enable)
{
	struct cmd_enabledisable_path *cmd;
	int ret;
	u16 cmd_rx, cmd_tx;

	wl1271_debug(DEBUG_CMD, "cmd data path");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	/* the channel here is only used for calibration, so hardcoded to 1 */
	cmd->channel = 1;

	if (enable) {
		cmd_rx = CMD_ENABLE_RX;
		cmd_tx = CMD_ENABLE_TX;
	} else {
		cmd_rx = CMD_DISABLE_RX;
		cmd_tx = CMD_DISABLE_TX;
	}

	ret = wl1271_cmd_send(wl, cmd_rx, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("rx %s cmd for channel %d failed",
			     enable ? "start" : "stop", cmd->channel);
		goto out;
	}

	wl1271_debug(DEBUG_BOOT, "rx %s cmd channel %d",
		     enable ? "start" : "stop", cmd->channel);

	ret = wl1271_cmd_send(wl, cmd_tx, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("tx %s cmd for channel %d failed",
			     enable ? "start" : "stop", cmd->channel);
		goto out;
	}

	wl1271_debug(DEBUG_BOOT, "tx %s cmd channel %d",
		     enable ? "start" : "stop", cmd->channel);

out:
	kfree(cmd);
	return ret;
}

int wl1271_cmd_ps_mode(struct wl1271 *wl, u8 ps_mode)
{
	struct wl1271_cmd_ps_params *ps_params = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set ps mode");

	ps_params = kzalloc(sizeof(*ps_params), GFP_KERNEL);
	if (!ps_params) {
		ret = -ENOMEM;
		goto out;
	}

	ps_params->role_id = wl->role_id;
	ps_params->ps_mode = ps_mode;

	ret = wl1271_cmd_send(wl, CMD_SET_PS_MODE, ps_params,
			      sizeof(*ps_params), 0);
	if (ret < 0) {
		wl1271_error("cmd set_ps_mode failed");
		goto out;
	}

out:
	kfree(ps_params);
	return ret;
}

int wl1271_cmd_template_set(struct wl1271 *wl, u16 template_id,
			    void *buf, size_t buf_len, int index, u32 rates)
{
	struct wl1271_cmd_template_set *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd template_set %d", template_id);

	WARN_ON(buf_len > WL1271_CMD_TEMPL_MAX_SIZE);
	buf_len = min_t(size_t, buf_len, WL1271_CMD_TEMPL_MAX_SIZE);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->len = cpu_to_le16(buf_len);
	cmd->template_type = template_id;
	cmd->enabled_rates = cpu_to_le32(rates);
	cmd->short_retry_limit = wl->conf.tx.tmpl_short_retry_limit;
	cmd->long_retry_limit = wl->conf.tx.tmpl_long_retry_limit;
	cmd->index = index;

	if (buf)
		memcpy(cmd->template_data, buf, buf_len);

	ret = wl1271_cmd_send(wl, CMD_SET_TEMPLATE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("cmd set_template failed: %d", ret);
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl1271_cmd_build_null_data(struct wl1271 *wl)
{
	struct sk_buff *skb = NULL;
	int size;
	void *ptr;
	int ret = -ENOMEM;


	if (wl->bss_type == BSS_TYPE_IBSS) {
		size = sizeof(struct wl12xx_null_data_template);
		ptr = NULL;
	} else {
		skb = ieee80211_nullfunc_get(wl->hw, wl->vif);
		if (!skb)
			goto out;
		size = skb->len;
		ptr = skb->data;
	}

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_NULL_DATA, ptr, size, 0,
				      wl->basic_rate);

out:
	dev_kfree_skb(skb);
	if (ret)
		wl1271_warning("cmd buld null data failed %d", ret);

	return ret;

}

int wl1271_cmd_build_klv_null_data(struct wl1271 *wl)
{
	struct sk_buff *skb = NULL;
	int ret = -ENOMEM;

	skb = ieee80211_nullfunc_get(wl->hw, wl->vif);
	if (!skb)
		goto out;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_KLV,
				      skb->data, skb->len,
				      CMD_TEMPL_KLV_IDX_NULL_DATA,
				      wl->basic_rate);

out:
	dev_kfree_skb(skb);
	if (ret)
		wl1271_warning("cmd build klv null data failed %d", ret);

	return ret;

}

int wl1271_cmd_build_ps_poll(struct wl1271 *wl, u16 aid)
{
	struct sk_buff *skb;
	int ret = 0;

	skb = ieee80211_pspoll_get(wl->hw, wl->vif);
	if (!skb)
		goto out;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_PS_POLL, skb->data,
				      skb->len, 0, wl->basic_rate_set);

out:
	dev_kfree_skb(skb);
	return ret;
}

int wl1271_cmd_build_probe_req(struct wl1271 *wl,
			       const u8 *ssid, size_t ssid_len,
			       const u8 *ie, size_t ie_len, u8 band)
{
	struct sk_buff *skb;
	int ret;

	skb = ieee80211_probereq_get(wl->hw, wl->vif, ssid, ssid_len,
				     ie, ie_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_dump(DEBUG_SCAN, "PROBE REQ: ", skb->data, skb->len);

	if (band == IEEE80211_BAND_2GHZ)
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_2_4,
					      skb->data, skb->len, 0,
					      wl->conf.tx.basic_rate);
	else
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_5,
					      skb->data, skb->len, 0,
					      wl->conf.tx.basic_rate_5);

out:
	dev_kfree_skb(skb);
	return ret;
}

struct sk_buff *wl1271_cmd_build_ap_probe_req(struct wl1271 *wl,
					      struct sk_buff *skb)
{
	int ret;

	if (!skb)
		skb = ieee80211_ap_probereq_get(wl->hw, wl->vif);
	if (!skb)
		goto out;

	wl1271_dump(DEBUG_SCAN, "AP PROBE REQ: ", skb->data, skb->len);

	if (wl->band == IEEE80211_BAND_2GHZ)
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_2_4,
					      skb->data, skb->len, 0,
					      wl->conf.tx.basic_rate);
	else
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_5,
					      skb->data, skb->len, 0,
					      wl->conf.tx.basic_rate_5);

	if (ret < 0)
		wl1271_error("Unable to set ap probe request template.");

out:
	return skb;
}

int wl1271_cmd_build_arp_rsp(struct wl1271 *wl, __be32 ip_addr)
{
	int ret;
	struct wl12xx_arp_rsp_template tmpl;
	struct ieee80211_hdr_3addr *hdr;
	struct arphdr *arp_hdr;

	memset(&tmpl, 0, sizeof(tmpl));

	/* mac80211 header */
	hdr = &tmpl.hdr;
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					 IEEE80211_STYPE_DATA |
					 IEEE80211_FCTL_TODS);
	memcpy(hdr->addr1, wl->vif->bss_conf.bssid, ETH_ALEN);
	memcpy(hdr->addr2, wl->vif->addr, ETH_ALEN);
	memset(hdr->addr3, 0xff, ETH_ALEN);

	/* llc layer */
	memcpy(tmpl.llc_hdr, rfc1042_header, sizeof(rfc1042_header));
	tmpl.llc_type = cpu_to_be16(ETH_P_ARP);

	/* arp header */
	arp_hdr = &tmpl.arp_hdr;
	arp_hdr->ar_hrd = cpu_to_be16(ARPHRD_ETHER);
	arp_hdr->ar_pro = cpu_to_be16(ETH_P_IP);
	arp_hdr->ar_hln = ETH_ALEN;
	arp_hdr->ar_pln = 4;
	arp_hdr->ar_op = cpu_to_be16(ARPOP_REPLY);

	/* arp payload */
	memcpy(tmpl.sender_hw, wl->vif->addr, ETH_ALEN);
	tmpl.sender_ip = ip_addr;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_ARP_RSP,
				      &tmpl, sizeof(tmpl), 0,
				      wl->basic_rate);

	return ret;
}

int wl1271_build_qos_null_data(struct wl1271 *wl)
{
	struct ieee80211_qos_hdr template;

	memset(&template, 0, sizeof(template));

	memcpy(template.addr1, wl->bssid, ETH_ALEN);
	memcpy(template.addr2, wl->mac_addr, ETH_ALEN);
	memcpy(template.addr3, wl->bssid, ETH_ALEN);

	template.frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					     IEEE80211_STYPE_QOS_NULLFUNC |
					     IEEE80211_FCTL_TODS);

	/* FIXME: not sure what priority to use here */
	template.qos_ctrl = cpu_to_le16(0);

	return wl1271_cmd_template_set(wl, CMD_TEMPL_QOS_NULL_DATA, &template,
				       sizeof(template), 0,
				       wl->basic_rate);
}

int wl12xx_cmd_set_default_wep_key(struct wl1271 *wl, u8 id, u8 hlid)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set_default_wep_key %d", id);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = hlid;
	cmd->key_id = id;
	cmd->lid_key_type = WEP_DEFAULT_LID_TYPE;
	cmd->key_action = cpu_to_le16(KEY_SET_ID);
	cmd->key_type = KEY_WEP;

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("cmd set_default_wep_key failed: %d", ret);
		goto out;
	}

out:
	kfree(cmd);

	return ret;
}

int wl1271_cmd_set_sta_key(struct wl1271 *wl, u16 action, u8 id, u8 key_type,
		       u8 key_size, const u8 *key, const u8 *addr,
		       u32 tx_seq_32, u16 tx_seq_16)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;

	/* hlid might have already been deleted */
	if (wl->sta_hlid == WL12XX_INVALID_LINK_ID)
		return 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = wl->sta_hlid;

	if (key_type == KEY_WEP)
		cmd->lid_key_type = WEP_DEFAULT_LID_TYPE;
	else if (is_broadcast_ether_addr(addr))
		cmd->lid_key_type = BROADCAST_LID_TYPE;
	else
		cmd->lid_key_type = UNICAST_LID_TYPE;

	cmd->key_action = cpu_to_le16(action);
	cmd->key_size = key_size;
	cmd->key_type = key_type;

	cmd->ac_seq_num16[0] = cpu_to_le16(tx_seq_16);
	cmd->ac_seq_num32[0] = cpu_to_le32(tx_seq_32);

	cmd->key_id = id;

	if (key_type == KEY_TKIP) {
		/*
		 * We get the key in the following form:
		 * TKIP (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 * but the target is expecting:
		 * TKIP - RX MIC - TX MIC
		 */
		memcpy(cmd->key, key, 16);
		memcpy(cmd->key + 16, key + 24, 8);
		memcpy(cmd->key + 24, key + 16, 8);

	} else {
		memcpy(cmd->key, key, key_size);
	}

	wl1271_dump(DEBUG_CRYPT, "TARGET KEY: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("could not set keys");
	goto out;
	}

out:
	kfree(cmd);

	return ret;
}

/*
 * TODO: merge with sta/ibss into 1 set_key function.
 * note there are slight diffs
 */
int wl1271_cmd_set_ap_key(struct wl1271 *wl, u16 action, u8 id, u8 key_type,
			u8 key_size, const u8 *key, u8 hlid, u32 tx_seq_32,
			u16 tx_seq_16)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;
	u8 lid_type;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	if (hlid == wl->ap_bcast_hlid) {
		if (key_type == KEY_WEP)
			lid_type = WEP_DEFAULT_LID_TYPE;
		else
			lid_type = BROADCAST_LID_TYPE;
	} else {
		lid_type = UNICAST_LID_TYPE;
	}

	wl1271_debug(DEBUG_CRYPT, "ap key action: %d id: %d lid: %d type: %d"
		     " hlid: %d", (int)action, (int)id, (int)lid_type,
		     (int)key_type, (int)hlid);

	cmd->lid_key_type = lid_type;
	cmd->hlid = hlid;
	cmd->key_action = cpu_to_le16(action);
	cmd->key_size = key_size;
	cmd->key_type = key_type;
	cmd->key_id = id;
	cmd->ac_seq_num16[0] = cpu_to_le16(tx_seq_16);
	cmd->ac_seq_num32[0] = cpu_to_le32(tx_seq_32);

	if (key_type == KEY_TKIP) {
		/*
		 * We get the key in the following form:
		 * TKIP (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 * but the target is expecting:
		 * TKIP - RX MIC - TX MIC
		 */
		memcpy(cmd->key, key, 16);
		memcpy(cmd->key + 16, key + 24, 8);
		memcpy(cmd->key + 24, key + 16, 8);
	} else {
		memcpy(cmd->key, key, key_size);
	}

	wl1271_dump(DEBUG_CRYPT, "TARGET AP KEY: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("could not set ap keys");
		goto out;
	}

out:
	kfree(cmd);
	return ret;
}

int wl12xx_cmd_set_peer_state(struct wl1271 *wl, u8 hlid)
{
	struct wl12xx_cmd_set_peer_state *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set peer state (hlid=%d)", hlid);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = hlid;
	cmd->state = WL1271_CMD_STA_STATE_CONNECTED;

	ret = wl1271_cmd_send(wl, CMD_SET_PEER_STATE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send set peer state command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_add_peer(struct wl1271 *wl, struct ieee80211_sta *sta, u8 hlid)
{
	struct wl12xx_cmd_add_peer *cmd;
	int i, ret;
	u32 sta_rates;

	wl1271_debug(DEBUG_CMD, "cmd add peer %d", (int)hlid);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(cmd->addr, sta->addr, ETH_ALEN);
	cmd->bss_index = WL1271_AP_BSS_INDEX;
	cmd->aid = sta->aid;
	cmd->hlid = hlid;
	cmd->sp_len = sta->max_sp;
	cmd->wmm = sta->wme ? 1 : 0;

	for (i = 0; i < NUM_ACCESS_CATEGORIES_COPY; i++)
		if (sta->wme && (sta->uapsd_queues & BIT(i)))
			cmd->psd_type[i] = WL1271_PSD_UPSD_TRIGGER;
		else
			cmd->psd_type[i] = WL1271_PSD_LEGACY;

	sta_rates = sta->supp_rates[wl->band];
	if (sta->ht_cap.ht_supported)
		sta_rates |= sta->ht_cap.mcs.rx_mask[0] << HW_HT_RATES_OFFSET;

	cmd->supported_rates =
		cpu_to_le32(wl1271_tx_enabled_rates_get(wl, sta_rates));

	wl1271_debug(DEBUG_CMD, "new peer rates=0x%x queues=0x%x",
		     cmd->supported_rates, sta->uapsd_queues);

	ret = wl1271_cmd_send(wl, CMD_ADD_PEER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd add peer");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_remove_peer(struct wl1271 *wl, u8 hlid)
{
	struct wl12xx_cmd_remove_peer *cmd;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd remove peer %d", (int)hlid);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->hlid = hlid;
	/* We never send a deauth, mac80211 is in charge of this */
	cmd->reason_opcode = 0;
	cmd->send_deauth_flag = 0;

	ret = wl1271_cmd_send(wl, CMD_REMOVE_PEER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd remove peer");
		goto out_free;
	}

	/*
	 * We are ok with a timeout here. The event is sometimes not sent
	 * due to a firmware bug.
	 */
	wl1271_cmd_wait_for_event_or_timeout(wl,
					     PEER_REMOVE_COMPLETE_EVENT_ID);

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_config_fwlog(struct wl1271 *wl)
{
	struct wl12xx_cmd_config_fwlog *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd config firmware logger");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->logger_mode = wl->conf.fwlog.mode;
	cmd->log_severity = wl->conf.fwlog.severity;
	cmd->timestamp = wl->conf.fwlog.timestamp;
	cmd->output = wl->conf.fwlog.output;
	cmd->threshold = wl->conf.fwlog.threshold;

	ret = wl1271_cmd_send(wl, CMD_CONFIG_FWLOGGER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send config firmware logger command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_start_fwlog(struct wl1271 *wl)
{
	struct wl12xx_cmd_start_fwlog *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd start firmware logger");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_send(wl, CMD_START_FWLOGGER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send start firmware logger command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_cmd_stop_fwlog(struct wl1271 *wl)
{
	struct wl12xx_cmd_stop_fwlog *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd stop firmware logger");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_send(wl, CMD_STOP_FWLOGGER, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send stop firmware logger command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wl12xx_cmd_roc(struct wl1271 *wl, u8 role_id)
{
	struct wl12xx_cmd_roc *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd roc %d (%d)", wl->channel, role_id);

	if (WARN_ON(role_id == WL12XX_INVALID_ROLE_ID))
		return -EINVAL;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->role_id = role_id;
	cmd->channel = wl->channel;
	switch (wl->band) {
	case IEEE80211_BAND_2GHZ:
		cmd->band = RADIO_BAND_2_4GHZ;
		break;
	case IEEE80211_BAND_5GHZ:
		cmd->band = RADIO_BAND_5GHZ;
		break;
	default:
		wl1271_error("roc - unknown band: %d", (int)wl->band);
		ret = -EINVAL;
		goto out_free;
	}


	ret = wl1271_cmd_send(wl, CMD_REMAIN_ON_CHANNEL, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send ROC command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wl12xx_cmd_croc(struct wl1271 *wl, u8 role_id)
{
	struct wl12xx_cmd_croc *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd croc (%d)", role_id);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}
	cmd->role_id = role_id;

	ret = wl1271_cmd_send(wl, CMD_CANCEL_REMAIN_ON_CHANNEL, cmd,
			      sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send ROC command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl12xx_roc(struct wl1271 *wl, u8 role_id)
{
	int ret = 0;

	if (WARN_ON(test_bit(role_id, wl->roc_map)))
		return 0;

	ret = wl12xx_cmd_roc(wl, role_id);
	if (ret < 0)
		goto out;

	ret = wl1271_cmd_wait_for_event(wl,
					REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID);
	if (ret < 0) {
		wl1271_error("cmd roc event completion error");
		goto out;
	}

	__set_bit(role_id, wl->roc_map);
out:
	return ret;
}

int wl12xx_croc(struct wl1271 *wl, u8 role_id)
{
	int ret = 0;

	if (WARN_ON(!test_bit(role_id, wl->roc_map)))
		return 0;

	ret = wl12xx_cmd_croc(wl, role_id);
	if (ret < 0)
		goto out;

	__clear_bit(role_id, wl->roc_map);
out:
	return ret;
}
