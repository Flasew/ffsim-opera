// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef _ECN_QUEUE_H
#define _ECN_QUEUE_H
#include "queue.h"
/*
 * A simple ECN queue that marks on dequeue as soon as the packet occupancy exceeds the set threshold. 
 */

#include <list>
#include "config.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"

class DynFlatScheduler;

class ECNQueue : public Queue {
 public:
    ECNQueue(linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist, 
		  QueueLogger* logger, mem_b drop);
    void receivePacket(Packet & pkt);
    void completeService();
    void set_dyn_sch(DynFlatScheduler * ds);
//  private:
    mem_b _K;
    int _state_send;
    DynFlatScheduler * dyn_sch = nullptr;
};

#endif
