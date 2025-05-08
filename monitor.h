#ifndef _MORSE_MONITOR_H_
#define _MORSE_MONITOR_H_

/*
 * Copyright 2017-2022 Morse Micro
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
#include <net/mac80211.h>

#include "command.h"
#include "morse.h"

int morse_mon_init(struct morse *mors);

void morse_mon_free(struct morse *mors);

void morse_mon_rx(struct morse *mors, struct sk_buff *rx_skb,
		  struct morse_skb_rx_status *hdr_rx_status);

void morse_mon_sig_field_error(const struct morse_evt_sig_field_error_evt *sig_field_error_evt);

#endif /* !_MORSE_MONITOR_H_ */
