/* -*- c++ -*- */
/*
 * Copyright 2010 Michael Ossmann
 * Copyright 2009, 2010 Mike Kershaw
 * 
 * This file is part of gr-bluetooth
 * 
 * gr-bluetooth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * gr-bluetooth is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with gr-bluetooth; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include <globalregistry.h>
#include <packetchain.h>

#include "packet_bluetooth.h"
#include "tracker_bluetooth.h"

extern int pack_comp_bluetooth;

enum BTBBDEV_fields {
	BTBBDEV_lap, BTBBDEV_firsttime,
	BTBBDEV_lasttime, BTBBDEV_packets,
	BTBBDEV_maxfield
};

const char *BTBBDEV_fields_text[] = {
	"lap", "firsttime",
	"lasttime", "packets",
	NULL
};

int Protocol_BTBBDEV(PROTO_PARMS) {
	bluetooth_network *net = (bluetooth_network *) data;
	ostringstream osstr;

	cache->Filled(field_vec->size());

	for (unsigned int x = 0; x < field_vec->size(); x++) {
		unsigned int fnum = (*field_vec)[x];

		if (fnum >= BTBBDEV_maxfield) {
			out_string = "Unknown field requested.";
			return -1;
		}

		osstr.str("");

		if (cache->Filled(fnum)) {
			out_string += cache->GetCache(fnum) + " ";
			continue;
		}

		switch (fnum) {
			case BTBBDEV_lap:
				// TODO - fix these for endian swaps, output as bytes in a fixed order ?
				osstr << net->lap;
				break;
			case BTBBDEV_firsttime:
				osstr << net->first_time;
				break;
			case BTBBDEV_lasttime:
				osstr << net->last_time;
				break;
			case BTBBDEV_packets:
				osstr << net->num_packets;
				break;
		}

		out_string += osstr.str() + " ";
		cache->Cache(fnum, osstr.str());
	}

	return 1;
}

void Protocol_BTBBDEV_enable(PROTO_ENABLE_PARMS) {
	((Tracker_Bluetooth *) data)->BlitDevices(in_fd);
}

int bttracktimer(TIMEEVENT_PARMS) {
	((Tracker_Bluetooth *) parm)->BlitDevices(-1);
	return 1;
}

int bluetooth_chain_hook(CHAINCALL_PARMS) {
	return ((Tracker_Bluetooth *) auxdata)->chain_handler(in_pack);
}

Tracker_Bluetooth::Tracker_Bluetooth(GlobalRegistry *in_globalreg) {
	globalreg = in_globalreg;

	globalreg->packetchain->RegisterHandler(&bluetooth_chain_hook, this,
											CHAINPOS_CLASSIFIER, 0);

	BTBBDEV_ref = 
		globalreg->kisnetserver->RegisterProtocol("BTBBDEV", 0, 1,
												  BTBBDEV_fields_text,
												  &Protocol_BTBBDEV,
												  &Protocol_BTBBDEV_enable,
												  this);

	timer_ref =
		globalreg->timetracker->RegisterTimer(SERVER_TIMESLICES_SEC, NULL, 1,
											  &bttracktimer, this);
}

int Tracker_Bluetooth::chain_handler(kis_packet *in_pack) {
	bluetooth_packinfo *pi = (bluetooth_packinfo *) in_pack->fetch(pack_comp_bluetooth);

	if (pi == NULL)
		return 0;

	uint32_t lap = pi->lap;
	bluetooth_network *net = NULL;

	map<uint32_t, bluetooth_network *>::iterator titr = tracked_nets.find(lap);

	if (titr == tracked_nets.end()) {
		printf("new network %06x\n", lap);
		net = new bluetooth_network();
		net->first_time = globalreg->timestamp.tv_sec;
		net->lap = lap;

		tracked_nets[lap] = net;
	} else {
		net = titr->second;
	}

	net->dirty = 1;

	net->last_time = globalreg->timestamp.tv_sec;
	net->num_packets++;

	return 1;
}

void Tracker_Bluetooth::BlitDevices(int in_fd) {
	map<uint32_t, bluetooth_network *>::iterator x;

	for (x = tracked_nets.begin(); x != tracked_nets.end(); x++) {
		kis_protocol_cache cache;

		if (in_fd == -1) {
			if (x->second->dirty == 0)
				continue;

			x->second->dirty = 0;

			if (globalreg->kisnetserver->SendToAll(BTBBDEV_ref,
												   (void *) x->second) < 0)
				break;

		} else {
			if (globalreg->kisnetserver->SendToClient(in_fd, BTBBDEV_ref,
													  (void *) x->second,
													  &cache) < 0)
				break;
		}
	}
}