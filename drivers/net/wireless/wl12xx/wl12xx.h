/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2009 Nokia Corporation
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

#ifndef __WL12XX_H__
#define __WL12XX_H__

#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <net/mac80211.h>

#include "conf.h"
#include "ini.h"

#define DRIVER_NAME "wl1271"
#define DRIVER_PREFIX DRIVER_NAME ": "

/*
 * FW versions support BA 11n
 * versions marks x.x.x.50-60.x
 */
#define WL12XX_BA_SUPPORT_FW_COST_VER2_START    50
#define WL12XX_BA_SUPPORT_FW_COST_VER2_END      60

enum {
	DEBUG_NONE	= 0,
	DEBUG_IRQ	= BIT(0),
	DEBUG_SPI	= BIT(1),
	DEBUG_BOOT	= BIT(2),
	DEBUG_MAILBOX	= BIT(3),
	DEBUG_TESTMODE	= BIT(4),
	DEBUG_EVENT	= BIT(5),
	DEBUG_TX	= BIT(6),
	DEBUG_RX	= BIT(7),
	DEBUG_SCAN	= BIT(8),
	DEBUG_CRYPT	= BIT(9),
	DEBUG_PSM	= BIT(10),
	DEBUG_MAC80211	= BIT(11),
	DEBUG_CMD	= BIT(12),
	DEBUG_ACX	= BIT(13),
	DEBUG_SDIO	= BIT(14),
	DEBUG_FILTERS   = BIT(15),
	DEBUG_ADHOC     = BIT(16),
	DEBUG_AP	= BIT(17),
	DEBUG_MASTER	= (DEBUG_ADHOC | DEBUG_AP),
	DEBUG_ALL	= ~0,
};

extern u32 wl12xx_debug_level;

#define DEBUG_DUMP_LIMIT 1024

#define wl1271_error(fmt, arg...) \
	pr_err(DRIVER_PREFIX "ERROR " fmt "\n", ##arg)

#define wl1271_warning(fmt, arg...) \
	pr_warning(DRIVER_PREFIX "WARNING " fmt "\n", ##arg)

#define wl1271_notice(fmt, arg...) \
	pr_info(DRIVER_PREFIX fmt "\n", ##arg)

#define wl1271_info(fmt, arg...) \
	pr_info(DRIVER_PREFIX fmt "\n", ##arg)

#define wl1271_debug(level, fmt, arg...) \
	do { \
		if (level & wl12xx_debug_level) \
			pr_debug(DRIVER_PREFIX fmt "\n", ##arg); \
	} while (0)

/* TODO: use pr_debug_hex_dump when it will be available */
#define wl1271_dump(level, prefix, buf, len)	\
	do { \
		if (level & wl12xx_debug_level) \
			print_hex_dump(KERN_DEBUG, DRIVER_PREFIX prefix, \
				       DUMP_PREFIX_OFFSET, 16, 1,	\
				       buf,				\
				       min_t(size_t, len, DEBUG_DUMP_LIMIT), \
				       0);				\
	} while (0)

#define wl1271_dump_ascii(level, prefix, buf, len)	\
	do { \
		if (level & wl12xx_debug_level) \
			print_hex_dump(KERN_DEBUG, DRIVER_PREFIX prefix, \
				       DUMP_PREFIX_OFFSET, 16, 1,	\
				       buf,				\
				       min_t(size_t, len, DEBUG_DUMP_LIMIT), \
				       true);				\
	} while (0)

#define WL127X_FW_NAME "ti-connectivity/wl127x-fw-3.bin"
#define WL128X_FW_NAME "ti-connectivity/wl128x-fw-3.bin"

/*
 * wl127x and wl128x are using the same NVS file name. However, the
 * ini parameters between them are different.  The driver validates
 * the correct NVS size in wl1271_boot_upload_nvs().
 */
#define WL12XX_NVS_NAME "ti-connectivity/wl1271-nvs.bin"

#define WL1271_TX_SECURITY_LO16(s) ((u16)((s) & 0xffff))
#define WL1271_TX_SECURITY_HI32(s) ((u32)(((s) >> 16) & 0xffffffff))
#define WL1271_TX_SQN_POST_RECOVERY_PADDING 0xff

#define WL1271_CIPHER_SUITE_GEM 0x00147201

#define WL1271_BUSY_WORD_CNT 1
#define WL1271_BUSY_WORD_LEN (WL1271_BUSY_WORD_CNT * sizeof(u32))

#define WL1271_ELP_HW_STATE_ASLEEP 0
#define WL1271_ELP_HW_STATE_IRQ    1

#define WL1271_DEFAULT_BEACON_INT  100
#define WL1271_DEFAULT_DTIM_PERIOD 1

#define WL12XX_MAX_ROLES           4
#define WL12XX_MAX_LINKS           8
#define WL12XX_INVALID_ROLE_ID     0xff
#define WL12XX_INVALID_LINK_ID     0xff

/* Defined by FW as 0. Will not be freed or allocated. */
#define WL12XX_SYSTEM_HLID         0

/*
 * TODO: we currently don't support multirole. remove
 * this constant from the code when we do.
 */
#define WL1271_AP_STA_HLID_START   3

/*
 * When in AP-mode, we allow (at least) this number of packets
 * to be transmitted to FW for a STA in PS-mode. Only when packets are
 * present in the FW buffers it will wake the sleeping STA. We want to put
 * enough packets for the driver to transmit all of its buffered data before
 * the STA goes to sleep again. But we don't want to take too much memory
 * as it might hurt the throughput of active STAs.
 */
#define WL1271_PS_STA_MAX_PACKETS  2

#define WL1271_AP_BSS_INDEX        0
#define WL1271_AP_DEF_BEACON_EXP   20

#define ACX_TX_DESCRIPTORS         16

#define WL1271_AGGR_BUFFER_SIZE (4 * PAGE_SIZE)

enum wl1271_state {
	WL1271_STATE_OFF,
	WL1271_STATE_ON,
	WL1271_STATE_PLT,
};

enum wl1271_partition_type {
	PART_DOWN,
	PART_WORK,
	PART_DRPW,

	PART_TABLE_LEN
};

struct wl1271_partition {
	u32 size;
	u32 start;
};

struct wl1271_partition_set {
	struct wl1271_partition mem;
	struct wl1271_partition reg;
	struct wl1271_partition mem2;
	struct wl1271_partition mem3;
};

struct wl1271;

enum {
	FW_VER_CHIP,
	FW_VER_IF_TYPE,
	FW_VER_MAJOR,
	FW_VER_SUBTYPE,
	FW_VER_MINOR,

	NUM_FW_VER
};

#define FW_VER_CHIP_WL127X 6
#define FW_VER_CHIP_WL128X 7

#define FW_VER_IF_TYPE_STA 1
#define FW_VER_IF_TYPE_AP  2

#define FW_VER_MINOR_1_SPARE_STA_MIN 58
#define FW_VER_MINOR_1_SPARE_AP_MIN  47

#define FW_VER_MINOR_FWLOG_STA_MIN 70

struct wl1271_chip {
	u32 id;
	char fw_ver_str[ETHTOOL_BUSINFO_LEN];
	unsigned int fw_ver[NUM_FW_VER];
};

struct wl1271_stats {
	struct acx_statistics *fw_stats;
	unsigned long fw_stats_update;

	unsigned int retry_count;
	unsigned int excessive_retries;
};

#define NUM_TX_QUEUES              4
#define NUM_RX_PKT_DESC            8

#define AP_MAX_STATIONS            8

/* Broadcast and Global links + system link + links to stations */
/*
 * TODO: when WL1271_AP_STA_HLID_START is no longer constant, change all
 * the places that use this.
 */
#define AP_MAX_LINKS               (AP_MAX_STATIONS + WL1271_AP_STA_HLID_START)

/* FW status registers */
struct wl12xx_fw_status {
	__le32 intr;
	u8  fw_rx_counter;
	u8  drv_rx_counter;
	u8  reserved;
	u8  tx_results_counter;
	__le32 rx_pkt_descs[NUM_RX_PKT_DESC];
	__le32 fw_localtime;

	/*
	 * A bitmap (where each bit represents a single HLID)
	 * to indicate if the station is in PS mode.
	 */
	__le32 link_ps_bitmap;

	/*
	 * A bitmap (where each bit represents a single HLID) to indicate
	 * if the station is in Fast mode
	 */
	__le32 link_fast_bitmap;

	/* Cumulative counter of total released mem blocks since FW-reset */
	__le32 total_released_blks;

	/* Size (in Memory Blocks) of TX pool */
	__le32 tx_total;

	/* Cumulative counter of released packets per AC */
	u8 tx_released_pkts[NUM_TX_QUEUES];

	/* Cumulative counter of freed packets per HLID */
	u8 tx_lnk_free_pkts[WL12XX_MAX_LINKS];

	/* Cumulative counter of released Voice memory blocks */
	u8 tx_voice_released_blks;
	u8 padding_1[7];
	__le32 log_start_addr;
} __packed;

struct wl1271_rx_mem_pool_addr {
	u32 addr;
	u32 addr_extra;
};

#define WL1271_MAX_CHANNELS 64
struct wl1271_scan {
	struct cfg80211_scan_request *req;
	unsigned long scanned_ch[BITS_TO_LONGS(WL1271_MAX_CHANNELS)];
	bool failed;
	u8 state;
	u8 ssid[IEEE80211_MAX_SSID_LEN+1];
	size_t ssid_len;
};

struct wl1271_if_operations {
	void (*read)(struct wl1271 *wl, int addr, void *buf, size_t len,
		     bool fixed);
	void (*write)(struct wl1271 *wl, int addr, void *buf, size_t len,
		     bool fixed);
	void (*reset)(struct wl1271 *wl);
	void (*init)(struct wl1271 *wl);
	int (*power)(struct wl1271 *wl, bool enable);
	struct device* (*dev)(struct wl1271 *wl);
	void (*enable_irq)(struct wl1271 *wl);
	void (*disable_irq)(struct wl1271 *wl);
	void (*set_block_size) (struct wl1271 *wl, unsigned int blksz);
};

#define MAX_NUM_KEYS 14
#define MAX_KEY_SIZE 32

struct wl1271_ap_key {
	u8 id;
	u8 key_type;
	u8 key_size;
	u8 key[MAX_KEY_SIZE];
	u8 hlid;
	u32 tx_seq_32;
	u16 tx_seq_16;
};

enum wl12xx_flags {
	WL1271_FLAG_STA_ASSOCIATED,
	WL1271_FLAG_IBSS_JOINED,
	WL1271_FLAG_GPIO_POWER,
	WL1271_FLAG_TX_QUEUE_STOPPED,
	WL1271_FLAG_TX_PENDING,
	WL1271_FLAG_IN_ELP,
	WL1271_FLAG_ELP_REQUESTED,
	WL1271_FLAG_PSM,
	WL1271_FLAG_PSM_REQUESTED,
	WL1271_FLAG_IRQ_RUNNING,
	WL1271_FLAG_IDLE,
	WL1271_FLAG_PSPOLL_FAILURE,
	WL1271_FLAG_STA_STATE_SENT,
	WL1271_FLAG_FW_TX_BUSY,
	WL1271_FLAG_AP_STARTED,
	WL1271_FLAG_IF_INITIALIZED,
	WL1271_FLAG_DUMMY_PACKET_PENDING,
	WL1271_FLAG_SUSPENDED,
	WL1271_FLAG_PENDING_WORK,
	WL1271_FLAG_SOFT_GEMINI,
	WL1271_FLAG_RX_STREAMING_STARTED,
	WL1271_FLAG_RECOVERY_IN_PROGRESS,
};

struct wl1271_link {
	/* AP-mode - TX queue per AC in link */
	struct sk_buff_head tx_queue[NUM_TX_QUEUES];

	/* accounting for allocated / freed packets in FW */
	u8 allocated_pkts;
	u8 prev_freed_pkts;

	u8 addr[ETH_ALEN];

	/* bitmap of TIDs where RX BA sessions are active for this link */
	u8 ba_bitmap;
};

struct wl1271 {
	struct platform_device *plat_dev;
	struct ieee80211_hw *hw;
	bool mac80211_registered;

	void *if_priv;

	struct wl1271_if_operations *if_ops;

	void (*set_power)(bool enable);
	int irq;
	int ref_clock;

	spinlock_t wl_lock;

	enum wl1271_state state;
	struct mutex mutex;

	unsigned long flags;

	struct wl1271_partition_set part;

	struct wl1271_chip chip;

	int cmd_box_addr;
	int event_box_addr;

	u8 *fw;
	size_t fw_len;
	void *nvs;
	size_t nvs_len;

	s8 hw_pg_ver;

	u8 bssid[ETH_ALEN];
	u8 mac_addr[ETH_ALEN];
	u8 bss_type;
	u8 set_bss_type;
	u8 p2p; /* we are using p2p role */
	u8 ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 ssid_len;
	int channel;
	u8 role_id;
	u8 dev_role_id;
	u8 system_hlid;
	u8 sta_hlid;
	u8 dev_hlid;
	u8 ap_global_hlid;
	u8 ap_bcast_hlid;

	unsigned long links_map[BITS_TO_LONGS(WL12XX_MAX_LINKS)];
	unsigned long roles_map[BITS_TO_LONGS(WL12XX_MAX_ROLES)];
	unsigned long roc_map[BITS_TO_LONGS(WL12XX_MAX_ROLES)];

	struct wl1271_acx_mem_map *target_mem_map;

	/* Accounting for allocated / available TX blocks on HW */
	u32 tx_blocks_freed;
	u32 tx_blocks_available;
	u32 tx_allocated_blocks;
	u32 tx_results_count;

	/* amount of spare TX blocks to use */
	u32 tx_spare_blocks;

	/* Accounting for allocated / available Tx packets in HW */
	u32 tx_pkts_freed[NUM_TX_QUEUES];
	u32 tx_allocated_pkts[NUM_TX_QUEUES];

	/* Transmitted TX packets counter for chipset interface */
	u32 tx_packets_count;

	/* Time-offset between host and chipset clocks */
	s64 time_offset;

	/* Session counter for the chipset */
	int session_counter;

	/* Frames scheduled for transmission, not handled yet */
	struct sk_buff_head tx_queue[NUM_TX_QUEUES];
	int tx_queue_count[NUM_TX_QUEUES];
	long stopped_queues_map;

	/* Frames received, not handled yet by mac80211 */
	struct sk_buff_head deferred_rx_queue;

	/* Frames sent, not returned yet to mac80211 */
	struct sk_buff_head deferred_tx_queue;

	struct work_struct tx_work;
	struct workqueue_struct *freezable_wq;

	/* Pending TX frames */
	unsigned long tx_frames_map[BITS_TO_LONGS(ACX_TX_DESCRIPTORS)];
	struct sk_buff *tx_frames[ACX_TX_DESCRIPTORS];
	int tx_frames_cnt;

	/*
	 * Security sequence number
	 *     bits 0-15: lower 16 bits part of sequence number
	 *     bits 16-47: higher 32 bits part of sequence number
	 *     bits 48-63: not in use
	 */
	u64 tx_security_seq;

	/* 8 bits of the last sequence number in use */
	u8 tx_security_last_seq_lsb;

	/* FW Rx counter */
	u32 rx_counter;

	/* Rx memory pool address */
	struct wl1271_rx_mem_pool_addr rx_mem_pool_addr;

	/* Intermediate buffer, used for packet aggregation */
	u8 *aggr_buf;

	/* Reusable dummy packet template */
	struct sk_buff *dummy_packet;

	/* Network stack work  */
	struct work_struct netstack_work;

	/* FW log buffer */
	u8 *fwlog;

	/* Number of valid bytes in the FW log buffer */
	ssize_t fwlog_size;

	/* Sysfs FW log entry readers wait queue */
	wait_queue_head_t fwlog_waitq;

	/* Hardware recovery work */
	struct work_struct recovery_work;

	/* The mbox event mask */
	u32 event_mask;

	/* Mailbox pointers */
	u32 mbox_ptr[2];

	/* Are we currently scanning */
	struct wl1271_scan scan;
	struct delayed_work scan_complete_work;

	bool sched_scanning;

	/* probe-req template for the current AP */
	struct sk_buff *probereq;

	/* Our association ID */
	u16 aid;

	/*
	 * currently configured rate set:
	 *	bits  0-15 - 802.11abg rates
	 *	bits 16-23 - 802.11n   MCS index mask
	 * support only 1 stream, thus only 8 bits for the MCS rates (0-7).
	 */
	u32 basic_rate_set;
	u32 basic_rate;
	u32 rate_set;

	/* The current band */
	enum ieee80211_band band;

	/* Beaconing interval (needed for ad-hoc) */
	u32 beacon_int;

	/* Default key (for WEP) */
	u32 default_key;

	/* Rx Streaming */
	struct work_struct rx_streaming_enable_work;
	struct work_struct rx_streaming_disable_work;
	struct timer_list rx_streaming_timer;

	struct completion *elp_compl;
	struct completion *ps_compl;
	struct delayed_work elp_work;
	struct delayed_work pspoll_work;

	/* counter for ps-poll delivery failures */
	int ps_poll_failures;

	/* retry counter for PSM entries */
	u8 psm_entry_retry;

	/* in dBm */
	int power_level;

	int rssi_thold;
	int last_rssi_event;

	struct wl1271_stats stats;

	__le32 buffer_32;
	u32 buffer_cmd;
	u32 buffer_busyword[WL1271_BUSY_WORD_CNT];

	struct wl12xx_fw_status *fw_status;
	struct wl1271_tx_hw_res_if *tx_res_if;

	struct ieee80211_vif *vif;

	/* Current chipset configuration */
	struct conf_drv_settings conf;

	bool sg_enabled;

	bool enable_11a;

	struct list_head list;

	/* Most recently reported noise in dBm */
	s8 noise;

	/* map for HLIDs of associated stations - when operating in AP mode */
	unsigned long ap_hlid_map[BITS_TO_LONGS(AP_MAX_STATIONS)];

	/* recoreded keys for AP-mode - set here before AP startup */
	struct wl1271_ap_key *recorded_ap_keys[MAX_NUM_KEYS];

	/* bands supported by this instance of wl12xx */
	struct ieee80211_supported_band bands[IEEE80211_NUM_BANDS];

	/* RX BA constraint value */
	bool ba_support;
	u8 ba_rx_bitmap;
	bool ba_allowed;

	int tcxo_clock;

	/*
	 * wowlan trigger was configured during suspend.
	 * (currently, only "ANY" trigger is supported)
	 */
	bool wow_enabled;
	bool irq_wake_enabled;

	/*
	 * AP-mode - links indexed by HLID. The global and broadcast links
	 * are always active.
	 */
	struct wl1271_link links[AP_MAX_LINKS];

	/* the hlid of the link where the last transmitted skb came from */
	int last_tx_hlid;

	/* AP-mode - a bitmap of links currently in PS mode according to FW */
	u32 ap_fw_ps_map;

	/* AP-mode - a bitmap of links currently in PS mode in mac80211 */
	unsigned long ap_ps_map;

	/* Quirks of specific hardware revisions */
	unsigned int quirks;

	/* Platform limitations */
	unsigned int platform_quirks;

	/* number of currently active RX BA sessions */
	int ba_rx_session_count;

	/* AP-mode - number of currently connected stations */
	int active_sta_count;
};

struct wl1271_station {
	u8 hlid;
};

int wl1271_plt_start(struct wl1271 *wl);
int wl1271_plt_stop(struct wl1271 *wl);
int wl1271_recalc_rx_streaming(struct wl1271 *wl);
void wl12xx_queue_recovery_work(struct wl1271 *wl);
size_t wl12xx_copy_fwlog(struct wl1271 *wl, u8 *memblock, size_t maxlen);

#define JOIN_TIMEOUT 5000 /* 5000 milliseconds to join */

#define SESSION_COUNTER_MAX 7 /* maximum value for the session counter */

#define WL1271_DEFAULT_POWER_LEVEL 0

#define WL1271_TX_QUEUE_LOW_WATERMARK  32
#define WL1271_TX_QUEUE_HIGH_WATERMARK 256

#define WL1271_DEFERRED_QUEUE_LIMIT    64

/* WL1271 needs a 200ms sleep after power on, and a 20ms sleep before power
   on in case is has been shut down shortly before */
#define WL1271_PRE_POWER_ON_SLEEP 20 /* in milliseconds */
#define WL1271_POWER_ON_SLEEP 200 /* in milliseconds */

/* Macros to handle wl1271.sta_rate_set */
#define HW_BG_RATES_MASK	0xffff
#define HW_HT_RATES_OFFSET	16

/* Quirks */

/* Each RX/TX transaction requires an end-of-transaction transfer */
#define WL12XX_QUIRK_END_OF_TRANSACTION		BIT(0)

/* WL128X requires aggregated packets to be aligned to the SDIO block size */
#define WL12XX_QUIRK_BLOCKSIZE_ALIGNMENT	BIT(2)

/*
 * WL127X AP mode requires Low Power DRPw (LPD) enable to reduce power
 * consumption
 */
#define WL12XX_QUIRK_LPD_MODE                   BIT(3)

/* Older firmwares did not implement the FW logger over bus feature */
#define WL12XX_QUIRK_FWLOG_NOT_IMPLEMENTED	BIT(4)

#define WL12XX_HW_BLOCK_SIZE	256

#endif
