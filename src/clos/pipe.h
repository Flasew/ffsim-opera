// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef PIPE_H
#define PIPE_H

/*
 * A pipe is a dumb device which simply delays all incoming packets
 */

#include <list>
#include <utility>
#include "config.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"

class DynFlatScheduler;
class Pipe : public EventSource, public PacketSink {
 public:
    Pipe(simtime_picosec delay, EventList& eventlist);
    void receivePacket(Packet& pkt); // inherited from PacketSink
    void doNextEvent(); // inherited from EventSource
    simtime_picosec delay() { return _delay; }
    const string& nodename() { return _nodename; }

    void set_pipe_downlink() { _pipe_is_downlink = true; }
    bool _pipe_is_downlink;
    void set_dyn_sch(DynFlatScheduler * ds);

    uint64_t reportBytes(); // reports to the UtilMonitor
    uint64_t _B_delivered; // keep track of how many (non-hdr,ACK,NACK,PULL,RTX) packets were delivered to hosts
    DynFlatScheduler * dyn_sch = nullptr;

    simtime_picosec _delay;
    typedef pair<simtime_picosec,Packet*> pktrecord_t;
    list<pktrecord_t> _inflight; // the packets in flight (or being serialized)
    string _nodename;
};


#endif
