/*
 * Copyright 2022-2023 Morse Micro
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 */

#include <linux/slab.h>
#include <linux/timer.h>
#include "morse.h"
#include "utils.h"
#include "mac.h"
#include "bus.h"
#include "debug.h"
#include "rc.h"
#include "pv1.h"

/* Enable/Disable the fixed rate (Disabled by default) */
static bool enable_fixed_rate __read_mostly;
module_param(enable_fixed_rate, bool, 0644);
MODULE_PARM_DESC(enable_fixed_rate, "Enable the fixed rate");

/* Set the fixed mcs (Take effect when enable_fixed_rate is activated) */
static int fixed_mcs __read_mostly = 4;
module_param(fixed_mcs, int, 0644);
MODULE_PARM_DESC(fixed_mcs, "Set the fixed mcs (work when enable_fixed_rate is on)");

/* Set the fixed bandwidth (Take effect when enable_fixed_rate is activated) */
static int fixed_bw __read_mostly = 2;
module_param(fixed_bw, int, 0644);
MODULE_PARM_DESC(fixed_bw, "Set the fixed bandwidth (work when enable_fixed_rate is on)");

/* Set the fixed spatial stream (Take effect when enable_fixed_rate is activated) */
static int fixed_ss __read_mostly = 1;
module_param(fixed_ss, int, 0644);
MODULE_PARM_DESC(fixed_ss,
		 "Set the fixed spatial stream value (work when enable_fixed_rate is on)");

/* Set the fixed guard (Take effect when enable_fixed_rate is activated) */
static int fixed_guard __read_mostly;
module_param(fixed_guard, int, 0644);
MODULE_PARM_DESC(fixed_guard, "Set the fixed guard value (work when enable_fixed_rate is on)");

#define MORSE_RC_MMRC_BW_TO_FLAGS(X)				\
	(((X) == MMRC_BW_1MHZ) ? MORSE_SKB_RATE_FLAGS_1MHZ :	\
	((X) == MMRC_BW_2MHZ) ? MORSE_SKB_RATE_FLAGS_2MHZ :	\
	((X) == MMRC_BW_4MHZ) ? MORSE_SKB_RATE_FLAGS_4MHZ :	\
	((X) == MMRC_BW_8MHZ) ? MORSE_SKB_RATE_FLAGS_8MHZ :	\
	MORSE_SKB_RATE_FLAGS_2MHZ)

#define MORSE_RC_BW_TO_MMRC_BW(X) \
	(((X) == 1) ? MMRC_BW_1MHZ : \
	((X) == 2) ? MMRC_BW_2MHZ : \
	((X) == 4) ? MMRC_BW_4MHZ : \
	((X) == 8) ? MMRC_BW_8MHZ : \
	MMRC_BW_2MHZ)

#define MORSE_RC_DBG(_m, _f, _a...)		morse_dbg(FEATURE_ID_RATECONTROL, _m, _f, ##_a)
#define MORSE_RC_INFO(_m, _f, _a...)		morse_info(FEATURE_ID_RATECONTROL, _m, _f, ##_a)
#define MORSE_RC_WARN(_m, _f, _a...)		morse_warn(FEATURE_ID_RATECONTROL, _m, _f, ##_a)
#define MORSE_RC_ERR(_m, _f, _a...)		morse_err(FEATURE_ID_RATECONTROL, _m, _f, ##_a)

#define MORSE_RC_WARN_RATELIMITED(_m, _f, _a...)		\
	morse_warn_ratelimited(FEATURE_ID_RATECONTROL, _m, _f, ##_a)

static void morse_rc_work(struct work_struct *work)
{
	struct morse_rc *mrc = container_of(work, struct morse_rc, work);
	struct list_head *pos;

	spin_lock_bh(&mrc->lock);

	list_for_each(pos, &mrc->stas) {
		struct morse_rc_sta *mrc_sta = container_of(pos, struct morse_rc_sta, list);
		unsigned long now = jiffies;

		mrc_sta->last_update = now;

		mmrc_update(mrc_sta->tb);
	}

	spin_unlock_bh(&mrc->lock);

	mod_timer(&mrc->timer, jiffies + msecs_to_jiffies(100));
}

#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
static void morse_rc_timer(unsigned long addr)
#else
static void morse_rc_timer(struct timer_list *t)
#endif
{
#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
	struct morse *mors = (struct morse *)addr;
#else
	struct morse_rc *mrc = from_timer(mrc, t, timer);
	struct morse *mors = mrc->mors;
#endif

	queue_work(mors->net_wq, &mors->mrc.work);
}

int morse_rc_init(struct morse *mors)
{
	MORSE_RC_WARN(mors, "rate control algorithm: 'MMRC'\n");
	INIT_LIST_HEAD(&mors->mrc.stas);
	spin_lock_init(&mors->mrc.lock);

	INIT_WORK(&mors->mrc.work, morse_rc_work);
#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
	init_timer(&mors->mrc.timer);
	mors->mrc.timer.data = (unsigned long)mors;
	mors->mrc.timer.function = morse_rc_timer;
	add_timer(&mors->mrc.timer);
#else
	timer_setup(&mors->mrc.timer, morse_rc_timer, 0);
#endif

	mors->mrc.mors = mors;
	mod_timer(&mors->mrc.timer, jiffies + msecs_to_jiffies(100));
	return 0;
}

int morse_rc_deinit(struct morse *mors)
{
	cancel_work_sync(&mors->mrc.work);
	del_timer_sync(&mors->mrc.timer);

	return 0;
}

static void morse_rc_sta_config_guard_per_bw(bool enable_sgi_rc,
					     struct ieee80211_sta *sta,
					     struct mmrc_sta_capabilities *caps)
{
	const struct ieee80211_sta_ht_cap *ht_cap = morse_mac_sta_ht_cap(sta);
	const struct ieee80211_sta_vht_cap *vht_cap = morse_mac_sta_vht_cap(sta);

	caps->guard = MMRC_MASK(MMRC_GUARD_LONG);

	if (!enable_sgi_rc)
		return;

	if (ht_cap->ht_supported) {
		if ((caps->bandwidth & MMRC_MASK(MMRC_BW_1MHZ)) &&
				(ht_cap->cap & IEEE80211_HT_CAP_SGI_20)) {
			caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_1MHZ);
			caps->guard |= MMRC_MASK(MMRC_GUARD_SHORT);
		}

		if ((caps->bandwidth & MMRC_MASK(MMRC_BW_2MHZ)) &&
				(ht_cap->cap & IEEE80211_HT_CAP_SGI_40)) {
			caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_2MHZ);
			caps->guard |= MMRC_MASK(MMRC_GUARD_SHORT);
		}
	}

	if (vht_cap->vht_supported) {
		if ((caps->bandwidth & MMRC_MASK(MMRC_BW_4MHZ)) &&
				(vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_80)) {
			caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_4MHZ);
			caps->guard |= MMRC_MASK(MMRC_GUARD_SHORT);
		}

		if ((caps->bandwidth & MMRC_MASK(MMRC_BW_8MHZ)) &&
				(vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_160)) {
			caps->sgi_per_bw |= SGI_PER_BW(MMRC_BW_8MHZ);
			caps->guard |= MMRC_MASK(MMRC_GUARD_SHORT);
		}
	}
}

static void morse_rc_sta_add_vht_sta_caps(struct morse *mors,
					  struct mmrc_sta_capabilities *caps,
					  struct ieee80211_sta_vht_cap *vht_cap)
{
	struct ieee80211_vht_mcs_info *vht_mcs = &vht_cap->vht_mcs;
	int nss_idx;

	for (nss_idx = 0; nss_idx < min(NL80211_VHT_NSS_MAX, MMRC_SPATIAL_STREAM_MAX); nss_idx++) {
		u8 rx_mcs = ((le16_to_cpu(vht_mcs->rx_mcs_map) >>
			      (nss_idx * S1G_CAP_BITS_PER_MCS_NSS)) & GENMASK(1, 0));
		u8 tx_mcs = ((le16_to_cpu(vht_mcs->tx_mcs_map) >>
			      (nss_idx * S1G_CAP_BITS_PER_MCS_NSS)) & GENMASK(1, 0));

		/* Use lowest common denominator for spatial streams. */
		if (rx_mcs == IEEE80211_VHT_MCS_NOT_SUPPORTED)
			tx_mcs = IEEE80211_VHT_MCS_NOT_SUPPORTED;
		else if (tx_mcs == IEEE80211_VHT_MCS_NOT_SUPPORTED)
			rx_mcs = IEEE80211_VHT_MCS_NOT_SUPPORTED;
		else if (rx_mcs > tx_mcs)
			rx_mcs = tx_mcs;
		else
			tx_mcs = rx_mcs;

		MORSE_RC_DBG(mors, "%s: %dSS - %d RX, %d TX",
			     __func__, NSS_IDX_TO_NSS(nss_idx), rx_mcs, tx_mcs);

		switch (rx_mcs) {
		case IEEE80211_VHT_MCS_NOT_SUPPORTED:
			if (nss_idx == 0)
				MORSE_RC_ERR(mors, "%s: One spatial stream must be supported",
					     __func__);
			continue;

			/* VHT to S1G MCS mapping is 9->9, 8->7, 7->2. */
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			caps->rates |= MMRC_MASK(MMRC_MCS9) | MMRC_MASK(MMRC_MCS8);
			fallthrough;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			caps->rates |= MMRC_MASK(MMRC_MCS7) | MMRC_MASK(MMRC_MCS6) |
			    MMRC_MASK(MMRC_MCS5) | MMRC_MASK(MMRC_MCS4) | MMRC_MASK(MMRC_MCS3);
			fallthrough;
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			caps->rates |= MMRC_MASK(MMRC_MCS2) | MMRC_MASK(MMRC_MCS1) |
			    MMRC_MASK(MMRC_MCS0) | MMRC_MASK(MMRC_MCS10);
			caps->spatial_streams |= MMRC_MASK(nss_idx);
			break;
		default:
			MORSE_RC_WARN_RATELIMITED(mors,
						  "%s: Invalid MCS 0x%02x for spatial stream %d",
						  __func__, rx_mcs, nss_idx);
		}
	}
}

static void morse_rc_sta_add_mandatory_caps(struct morse *mors, struct mmrc_sta_capabilities *caps)
{
	caps->rates |= MMRC_MASK(MMRC_MCS0) | MMRC_MASK(MMRC_MCS1);
	caps->rates |= MMRC_MASK(MMRC_MCS2) | MMRC_MASK(MMRC_MCS3);
	caps->rates |= MMRC_MASK(MMRC_MCS4) | MMRC_MASK(MMRC_MCS5);
	caps->rates |= MMRC_MASK(MMRC_MCS6) | MMRC_MASK(MMRC_MCS7);
	caps->rates |= MMRC_MASK(MMRC_MCS10);

	caps->spatial_streams |= MMRC_MASK(MMRC_SPATIAL_STREAM_1);
}

/**
 * @morse_rc_sta_vht_caps_available() - Check whether VHT STA capabilities contains valid
 *					information.
 *
 * @mors:	The morse chip struct.
 * @vht_cap:	STA VHT capabilities.
 * Return:	false if VHT capabilities are not supported or either MCS MAP is all 0,
 *		otherwise true.

 * Note this looks at the MCS maps for 5-8 streams to determine if the MCS map is valid, since those
 * are never mapped for S1G and zero indicates usage of that number of spatial streams.
 */
static bool morse_rc_sta_vht_caps_available(struct morse *mors,
					    struct ieee80211_sta_vht_cap *vht_cap)
{
	struct ieee80211_vht_mcs_info *vht_mcs = &vht_cap->vht_mcs;

	if (unlikely(!vht_cap->vht_supported))
		return false;

	if (unlikely(!vht_mcs->tx_mcs_map || !vht_mcs->rx_mcs_map))
		return false;

	return true;
}

int morse_rc_sta_add(struct morse *mors, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct ieee80211_sta_vht_cap *vht_cap = morse_mac_sta_vht_cap(sta);
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;
	struct mmrc_sta_capabilities caps;
	int oper_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;
	size_t table_mem_size;

	memset(&caps, 0, sizeof(caps));

	/* Get MCS capability information for the STA */
	MORSE_RC_DBG(mors, "%s: VHT Cap: 0x%08x (%s)", __func__,
		     vht_cap->cap, vht_cap->vht_supported ? "True" : "False");

	MORSE_RC_DBG(mors, "%s: VHT MCS:", __func__);

	if (morse_rc_sta_vht_caps_available(mors, vht_cap)) {
		MORSE_RC_DBG(mors, "%s: VHT MCS map available", __func__);
		morse_rc_sta_add_vht_sta_caps(mors, &caps, vht_cap);
	} else {
		/* IBSS doesn't support VHT STA CAPs which are usually filled during association.
		 * Use mandatory supported settings for now.
		 */
		if (vif->type == NL80211_IFTYPE_ADHOC)
			MORSE_RC_DBG(mors, "%s: ADHOC MCS", __func__);
		else
			MORSE_RC_WARN(mors, "%s: No VHT support or VHT MCS map empty", __func__);

		morse_rc_sta_add_mandatory_caps(mors, &caps);
	}

	MORSE_RC_DBG(mors, "%s: MMRC spatial streams: 0x%02x", __func__, caps.spatial_streams);

	/* Configure STA for support up to 8MHZ */
	while (oper_bw_mhz > 0) {
		caps.bandwidth |= MMRC_MASK(MORSE_RC_BW_TO_MMRC_BW(oper_bw_mhz));
		oper_bw_mhz >>= 1;
	}

	/* Configure STA for short and long guard */
	morse_rc_sta_config_guard_per_bw(mors->custom_configs.enable_sgi_rc, sta, &caps);

	/* Set max rates */
	if (mors->hw->max_rates > 0 && mors->hw->max_rates < IEEE80211_TX_MAX_RATES)
		caps.max_rates = mors->hw->max_rates;
	else
		caps.max_rates = IEEE80211_TX_MAX_RATES;

	/* Set max reties */
	if (mors->hw->max_rate_tries >= MMRC_MIN_CHAIN_ATTEMPTS &&
	    mors->hw->max_rate_tries < MMRC_MAX_CHAIN_ATTEMPTS)
		caps.max_retries = mors->hw->max_rate_tries;
	else
		caps.max_retries = MMRC_MAX_CHAIN_ATTEMPTS;

	MORSE_WARN_ON(FEATURE_ID_RATECONTROL, msta->rc.tb);
	kfree(msta->rc.tb);
	table_mem_size = mmrc_memory_required_for_caps(&caps);
	MORSE_RC_DBG(mors, "%s: Mem for table: %zd", __func__, table_mem_size);
	msta->rc.tb = kzalloc(table_mem_size, GFP_KERNEL);

	/* Initialise the STA in MMRC */
	mmrc_sta_init(msta->rc.tb, &caps, msta->avg_rssi);

	/* Set last update time */
	msta->rc.last_update = jiffies;

	spin_lock_bh(&mors->mrc.lock);
	list_add(&msta->rc.list, &mors->mrc.stas);
	spin_unlock_bh(&mors->mrc.lock);

	return 0;
}

void morse_rc_reinit_stas(struct morse *mors, struct ieee80211_vif *vif)
{
	struct list_head *pos;
	struct morse_vif *mors_vif = ieee80211_vif_to_morse_vif(vif);
	struct list_head *morse_sta_list = &mors_vif->ap->stas;

	MORSE_RC_INFO(mors, "%s: no_of_stations=%d\n", __func__, mors_vif->ap->num_stas);
	list_for_each(pos, morse_sta_list) {
		struct morse_sta *msta = list_entry(pos, struct morse_sta, list);
		int oper_bw_mhz = mors->custom_configs.channel_info.op_bw_mhz;
		struct ieee80211_sta *sta =
		    container_of((void *)msta, struct ieee80211_sta, drv_priv);

		if (msta) {
			MORSE_RC_INFO(mors,
				      "%s:Reinitialize the sta %pM with new op_bw=%d, ts=%ld\n",
				      __func__, sta->addr, oper_bw_mhz, jiffies);
		} else {
			MORSE_RC_WARN(mors, "%s:msta NULL\n", __func__);
			continue;
		}

		morse_rc_sta_remove(mors, sta);
		morse_rc_sta_add(mors, vif, sta);

		/* Set fixed rate */
		if (enable_fixed_rate)
			morse_rc_set_fixed_rate(mors, sta, fixed_mcs, fixed_bw, fixed_ss,
						fixed_guard);
	}
}

bool _morse_rc_set_fixed_rate(struct morse *mors,
			      struct ieee80211_sta *sta,
			      int mcs, int bw, int ss, int guard, const char *caller)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;
	struct list_head *pos;
	struct mmrc_rate fixed_rate;
	bool ret_val = true;

	MORSE_RC_DBG(mors, "%s: %s fixing rate to ss %d bw %d mcs %d guard %d\n", __func__, caller,
		     ss, bw, mcs, guard);

	fixed_rate.rate = mcs;
	fixed_rate.bw = bw;
	/* Code spatial streams is zero based while user starts at 1, like the real spatial
	 * streams.
	 */
	fixed_rate.ss = (ss - 1);
	fixed_rate.guard = guard;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = list_entry(pos, struct morse_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			ret_val = mmrc_set_fixed_rate(msta->rc.tb, fixed_rate);
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);

	if (!ret_val)
		MORSE_RC_ERR(mors, "%s failed, caller %s ss %d bw %d mcs %d guard %d\n",
			     __func__, caller, ss, bw, mcs, guard);

	return ret_val;
}

void morse_rc_sta_remove(struct morse *mors, struct ieee80211_sta *sta)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;

	if (!msta->rc.tb)
		return;

	spin_lock_bh(&mors->mrc.lock);
	kfree(msta->rc.tb);
	msta->rc.tb = NULL;
	list_del_init(&msta->rc.list);
	spin_unlock_bh(&mors->mrc.lock);
}

static void morse_rc_sta_fill_basic_rates(struct morse *mors,
					  struct morse_skb_tx_info *tx_info, int tx_bw)
{
	int i;
	enum dot11_bandwidth bw_idx = morse_ratecode_bw_mhz_to_bw_index(tx_bw);
	enum morse_rate_preamble pream = MORSE_RATE_PREAMBLE_S1G_SHORT;

	morse_ratecode_mcs_index_set(&tx_info->rates[0].morse_ratecode, 0);
	morse_ratecode_nss_index_set(&tx_info->rates[0].morse_ratecode, NSS_TO_NSS_IDX(1));
	morse_ratecode_bw_index_set(&tx_info->rates[0].morse_ratecode, bw_idx);
	if (bw_idx == DOT11_BANDWIDTH_1MHZ)
		pream = MORSE_RATE_PREAMBLE_S1G_1M;
	morse_ratecode_preamble_set(&tx_info->rates[0].morse_ratecode, pream);
	tx_info->rates[0].count = 4;

	for (i = 1; i < IEEE80211_TX_MAX_RATES; i++)
		tx_info->rates[i].count = 0;
}

static int morse_rc_sta_get_rates(struct morse *mors,
				  struct morse_sta *msta,
				  struct mmrc_rate_table *rates, size_t size)
{
	int ret = -ENOENT;
	struct list_head *pos;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = list_entry(pos, struct morse_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			ret = 0;
			mmrc_get_rates(msta->rc.tb, rates, size);
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);

	return ret;
}

void morse_rc_sta_fill_tx_rates(struct morse *mors,
				struct morse_skb_tx_info *tx_info,
				struct sk_buff *skb,
				struct ieee80211_sta *sta, int tx_bw, bool rts_allowed)
{
	int ret, i;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct morse_sta *msta;
	struct mmrc_rate_table rates;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	/* Check if we can use MMRC and MORSE rate code BW index interchangeably */
	BUILD_BUG_ON((MMRC_BW_1MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_1MHZ ||
		      MMRC_BW_2MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_2MHZ ||
		      MMRC_BW_4MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_4MHZ ||
		      MMRC_BW_16MHZ != (enum mmrc_bw)DOT11_BANDWIDTH_16MHZ));

	morse_rc_sta_fill_basic_rates(mors, tx_info, tx_bw);

	/* Use basic rates for non data packets */
	if (!sta || (!ieee80211_is_data_qos(hdr->frame_control) &&
		     !morse_dot11ah_is_pv1_qos_data(hdr->frame_control)))
		return;

	msta = (struct morse_sta *)sta->drv_priv;
	if (!msta)
		return;

	/* Use basic rates for initial EAPOL exchange when rate control has not yet converged */
	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	ret = morse_rc_sta_get_rates(mors, msta, &rates, skb->len);

	/* If station not found, go for basic rates */
	if (ret < 0)
		return;

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if (rates.rates[i].rate != MMRC_MCS_UNUSED) {
			u8 mcs = rates.rates[i].rate;
			u8 nss_index = rates.rates[i].ss;
			enum dot11_bandwidth bw_idx = (enum dot11_bandwidth)rates.rates[i].bw;
			enum morse_rate_preamble pream = MORSE_RATE_PREAMBLE_S1G_SHORT;

			morse_ratecode_bw_index_set(&tx_info->rates[i].morse_ratecode, bw_idx);
			morse_ratecode_mcs_index_set(&tx_info->rates[i].morse_ratecode, mcs);
			morse_ratecode_nss_index_set(&tx_info->rates[i].morse_ratecode, nss_index);
			if (bw_idx == DOT11_BANDWIDTH_1MHZ)
				pream = MORSE_RATE_PREAMBLE_S1G_1M;
			morse_ratecode_preamble_set(&tx_info->rates[i].morse_ratecode, pream);
			tx_info->rates[i].count = rates.rates[i].attempts;

			if (rts_allowed && (rates.rates[i].flags & BIT(MMRC_FLAGS_CTS_RTS)))
				morse_ratecode_enable_rts(&tx_info->rates[i].morse_ratecode);

			if (rates.rates[i].guard == MMRC_GUARD_SHORT)
				morse_ratecode_enable_sgi(&tx_info->rates[i].morse_ratecode);

			/* Update skb tx_info */
			info->control.rates[i].idx = rates.rates[i].rate;
			info->control.rates[i].count = rates.rates[i].attempts;
			/* FIXME: Need to a better way to pass information around */
			info->control.rates[i].flags = tx_info->rates[i].morse_ratecode;
		} else {
			info->control.rates[i].idx = MMRC_MCS_UNUSED;
			info->control.rates[i].count = 0;
			info->control.rates[i].flags = 0;
			tx_info->rates[i].count = 0;
		}
	}
}

static int morse_rc_sta_get_attempts(struct morse *mors, struct morse_skb_tx_status *tx_sts)
{
	int attempts = 0;
	int i;
	int count = min_t(int, MORSE_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);

	for (i = 0; i < count; i++) {
		if (tx_sts->rates[i].count > 0)
			attempts += tx_sts->rates[i].count;
		else
			break;
	}

	return attempts;
}

static void morse_rc_sta_set_rates(struct morse *mors,
				   struct morse_sta *msta,
				   struct mmrc_rate_table *rates,
				   int attempts,
				   bool is_agg_mode, u32 success, u32 failure)
{
	struct list_head *pos;

	spin_lock_bh(&mors->mrc.lock);
	list_for_each(pos, &mors->mrc.stas) {
		struct morse_rc_sta *mrc_sta = list_entry(pos, struct morse_rc_sta, list);

		if (&msta->rc == mrc_sta) {
			if (is_agg_mode)
				mmrc_feedback_agg(msta->rc.tb, rates, attempts, success, failure);
			else
				mmrc_feedback(msta->rc.tb, rates, attempts);
			break;
		}
	}
	spin_unlock_bh(&mors->mrc.lock);
}

void morse_rc_sta_feedback_rates(struct morse *mors,
				 struct sk_buff *skb, struct morse_skb_tx_status *tx_sts)
{
	int attempts;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *r = &txi->status.rates[0];
	int count = min_t(int, MORSE_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);
	int i;
	struct mmrc_rate_table rates;
	struct morse_sta *msta = NULL;
	struct ieee80211_vif *vif = NULL;
	u32 agg_success, agg_packets;

	/* Need the RCU lock to find a station, and must hold it until we're done with sta */
	rcu_read_lock();

	vif = txi->control.vif ? txi->control.vif : morse_get_vif_from_tx_status(mors, tx_sts);

	if (morse_dot11ah_is_pv1_qos_data(hdr->frame_control))
		sta = morse_pv1_find_sta(vif, (struct dot11ah_mac_pv1_hdr *)hdr);
	else
		sta = ieee80211_find_sta(vif, hdr->addr1);

	if (!sta)
		goto exit;

	msta = (struct morse_sta *)sta->drv_priv;

	if (!msta ||
		(!ieee80211_is_data_qos(hdr->frame_control) &&
			!morse_dot11ah_is_pv1_qos_data(hdr->frame_control)))
		/* use basic rates for non-data packets */
		goto exit;

	attempts = morse_rc_sta_get_attempts(mors, tx_sts);
	if (attempts <= 0)
		/* Did we really send the packet? */
		goto exit;

	/* Update mmrc rates struct from ieee80211_tx_info feedback */
	for (i = 0; i < count; i++) {
		rates.rates[i].rate = morse_ratecode_mcs_index_get(tx_sts->rates[i].morse_ratecode);
		rates.rates[i].ss = morse_ratecode_nss_index_get(tx_sts->rates[i].morse_ratecode);
		rates.rates[i].guard = morse_ratecode_sgi_get(tx_sts->rates[i].morse_ratecode);
		rates.rates[i].bw = morse_ratecode_bw_index_get(tx_sts->rates[i].morse_ratecode);
		rates.rates[i].flags = morse_ratecode_rts_get(tx_sts->rates[i].morse_ratecode);
		rates.rates[i].attempts = txi->control.rates[i].count;
	}

	if (msta) {
		/* Save the rate information. This will used to update stations tx rate stats */
		msta->last_sta_tx_rate.bw = rates.rates[0].bw;
		msta->last_sta_tx_rate.rate = rates.rates[0].rate;
		msta->last_sta_tx_rate.ss = rates.rates[0].ss;
		msta->last_sta_tx_rate.guard = rates.rates[0].guard;
	}

	if (tx_sts->ampdu_info) {
		agg_success = MORSE_TXSTS_AMPDU_INFO_GET_SUC(tx_sts->ampdu_info);
		agg_packets = MORSE_TXSTS_AMPDU_INFO_GET_LEN(tx_sts->ampdu_info);
		morse_rc_sta_set_rates(mors, msta, &rates, attempts, true, agg_success,
				       agg_packets - agg_success);
	} else {
		morse_rc_sta_set_rates(mors, msta, &rates, attempts, false, 0, 0);
	}

exit:
	ieee80211_tx_info_clear_status(txi);
	if (tx_sts) {
		if (!(tx_sts->flags & MORSE_TX_STATUS_FLAGS_NO_ACK) &&
		    !(txi->flags & IEEE80211_TX_CTL_NO_ACK))
			txi->flags |= IEEE80211_TX_STAT_ACK;

		if (tx_sts->flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED) {
			mors->debug.page_stats.tx_ps_filtered++;
			txi->flags |= IEEE80211_TX_STAT_TX_FILTERED;

			/* Clear TX CTL AMPDU flag so that this frame gets rescheduled in
			 * ieee80211_handle_filtered_frame(). This flag will get set
			 * again by mac80211's tx path on rescheduling.
			 */
			txi->flags &= ~IEEE80211_TX_CTL_AMPDU;
			if (msta) {
				if (!msta->tx_ps_filter_en)
					MORSE_RC_DBG(mors, "TX ps filter set sta[%pM]\n",
						     msta->addr);
				msta->tx_ps_filter_en = true;
			}
		}

		for (i = 0; i < count; i++) {
			if (tx_sts->rates[i].count > 0)
				r[i].count = tx_sts->rates[i].count;
			else
				r[i].idx = -1;
		}
	} else {
		txi->control.rates[0].count = 1;
		txi->control.rates[1].idx = -1;
		if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK))
			txi->flags |= IEEE80211_TX_STAT_ACK;
	}
	/* single packet per A-MPDU (for now) */
	if (txi->flags & IEEE80211_TX_CTL_AMPDU) {
		txi->flags |= IEEE80211_TX_STAT_AMPDU;
		txi->status.ampdu_len = 1;
		txi->status.ampdu_ack_len = txi->flags & IEEE80211_TX_STAT_ACK ? 1 : 0;
	}

	/* Inform mac80211 that the SP (elicited by a PS-Poll or u-APSD) is over */
	if (sta && (txi->flags & IEEE80211_TX_STATUS_EOSP)) {
		txi->flags &= ~IEEE80211_TX_STATUS_EOSP;
		ieee80211_sta_eosp(sta);
	}

	rcu_read_unlock();
#if KERNEL_VERSION(6, 7, 0) > MAC80211_VERSION_CODE
	ieee80211_tx_status(mors->hw, skb);
#else
	ieee80211_tx_status_skb(mors->hw, skb);
#endif
}

void morse_rc_sta_state_check(struct morse *mors,
			      struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			      enum ieee80211_sta_state old_state,
			      enum ieee80211_sta_state new_state)
{
	struct morse_sta *msta = (struct morse_sta *)sta->drv_priv;

	/* Add to Morse RC STA list */
	if (old_state < new_state && new_state == IEEE80211_STA_ASSOC) {
		/* Newly associated, add to RC */
		morse_rc_sta_add(mors, vif, sta);

		/* Set fixed rate */
		if (enable_fixed_rate)
			morse_rc_set_fixed_rate(mors, sta, fixed_mcs, fixed_bw, fixed_ss,
						fixed_guard);
	} else if (old_state > new_state &&
		   (old_state == IEEE80211_STA_ASSOC || old_state == IEEE80211_STA_AUTH)) {
		/* Lost or failed association; remove from list */
		morse_rc_sta_remove(mors, sta);
	} else if (old_state < new_state &&
		   old_state == IEEE80211_STA_NONE &&
		   msta->rc.list.prev) {
		/* Special case for driver warning issue causing a sta to be left on the list */
		MORSE_RC_INFO(mors, "Remove stale sta from rc list\n");
		morse_rc_sta_remove(mors, sta);
	}
}
