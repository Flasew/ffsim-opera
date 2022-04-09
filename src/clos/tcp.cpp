// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "tcp.h"
#include "mtcp.h"
#include "ecn.h"
#include "dyn_net_sch.h"
#include <iostream>
#include <fstream>

#define KILL_THRESHOLD 5
////////////////////////////////////////////////////////////////
//  TCP SOURCE
////////////////////////////////////////////////////////////////
DemandRecorder *TcpSrc::demand_recorder = nullptr;
bool TcpSrc::tcp_flow_paused = false;

TcpSrc::TcpSrc(TcpLogger *logger, TrafficLogger *pktlogger, ofstream *_fstream_out,
							 EventList &eventlist, int flow_src, int flow_dst,
							 void (*acf)(void *), void *acd)
		: EventSource(eventlist, "tcp"), _logger(logger), _flow(pktlogger), _flow_src(flow_src), _flow_dst(flow_dst), application_callback(acf), application_callback_data(acd)
{
	_mss = Packet::data_packet_size();
	_maxcwnd = 0xffffffff; // MAX_SENT*_mss;
	_sawtooth = 0;
	_subflow_id = -1;
	_rtt_avg = timeFromMs(0);
	_rtt_cum = timeFromMs(0);
	_base_rtt = timeInf;
	_cap = 0;
	_flow_size = ((uint64_t)1) << 63;
	_highest_sent = 0;
	_packets_sent = 0;
	_app_limited = -1;
	_established = false;
	_effcwnd = 0;
	_finished = false;

	fstream_out = _fstream_out;
	//_ssthresh = 30000;
	_ssthresh = 0xffffffff;

#ifdef MODEL_RECEIVE_WINDOW
	_highest_data_seq = 0;
#endif

	_last_acked = 0;
	_last_ping = timeInf;
	_dupacks = 0;
	_rtt = 0;
	_rto = timeFromMs(3000);
	_mdev = 0;
	_recoverq = 0;
	_in_fast_recovery = false;
	_mSrc = NULL;
	_drops = 0;

#ifdef PACKET_SCATTER
	_crt_path = 0;
	DUPACK_TH = 3;
	_paths = NULL;
#endif

	_old_route = NULL;
	_last_packet_with_old_route = 0;

	_rtx_timeout_pending = false;
	_RFC2988_RTO_timeout = timeInf;

	_nodename = "tcpsrc";
}

TcpSrc::~TcpSrc()
{
	if (_route)
		delete _route;
	if (_sink)
	{
		delete _sink;
	}
	if (_old_route)
	{
		delete _old_route;
	}
	// if (_mSrc) {
	//    delete _mSrc;
	// }
#ifdef PACKET_SCATTER
	if (_paths)
	{
		for (Route *r : _paths)
		{
			delete r;
		}
		delete _paths;
	}
#endif
}

#ifdef PACKET_SCATTER
void TcpSrc::set_paths(vector<const Route *> *rt)
{
	// this should only be used with route
	_paths = new vector<const Route *>();

	for (unsigned int i = 0; i < rt->size(); i++)
	{
		Route *t = new Route(*(rt->at(i)));
		t->push_back(_sink);
		_paths->push_back(t);
	}
	DUPACK_TH = 3 + rt->size();
	cout << "Setting DUPACK TH to " << DUPACK_TH << endl;
}
#endif

void TcpSrc::set_flowsize(uint64_t flow_size_in_bytes)
{

	_flow_size = flow_size_in_bytes; // + _mss; // not sure "+ _mss" is necessary...
																	 // if (_flow_size == 0)
																	 // 	_flow_size = 1;

	// NOTE: implement shorter packets?

	if (_flow_size < _mss)
		_flow_size = _mss;

	// if (demand_recorder)
	// 	demand_recorder->add_demand(_flow_src, _flow_dst, _flow_size);

	// !!! Note: need to implement this for short flows:

	// if (_flow_size < _mss)
	//    _mss = _flow_size;
	// else
	//  _pkt_size = _mss;
}

void TcpSrc::set_app_limit(int pktps)
{
	if (_app_limited == 0 && pktps)
	{
		_cwnd = _mss;
	}
	_ssthresh = 0xffffffff;
	_app_limited = pktps;
	if (!TcpSrc::tcp_flow_paused)
		send_packets();
	else
	{
		tcp_has_pending_send = true;
	}
}

void TcpSrc::startflow()
{
	_cwnd = 100 * _mss;
	// _cwnd = 1 << 23;
	_unacked = _cwnd;
	_established = false;

	if (!TcpSrc::tcp_flow_paused)
		send_packets();
	else
	{
		tcp_has_pending_send = true;
	}
}

uint32_t TcpSrc::effective_window()
{
	return _in_fast_recovery ? _ssthresh : _cwnd;
}

void TcpSrc::replace_route(const Route *newroute)
{

	if (_old_route)
		delete _old_route;

	_old_route = _route;
	_route = newroute;
	_last_packet_with_old_route = _highest_sent;
	_last_ping = timeInf;

	//  Printf("Wiating for ack %d to delete\n",_last_packet_with_old_route);
}

void TcpSrc::connect(const Route &routeout, const Route &routeback, TcpSink &sink,
										 simtime_picosec starttime)
{
	_route = &routeout;

	assert(_route);
	_sink = &sink;
	_flow.id = id; // identify the packet flow with the TCP source that generated it
	_sink->connect(*this, routeback);

	set_start_time(starttime); // record the start time in _start_time

	// printf("Tcp %x msrc %x\n",this,_mSrc);
	eventlist().sourceIsPending(*this, starttime);
}

#define ABS(X) ((X) > 0 ? (X) : -(X))

void TcpSrc::receivePacket(Packet &pkt)
{
	simtime_picosec ts;
	TcpAck *p = (TcpAck *)(&pkt);
	TcpAck::seq_t seqno = p->ackno();

#ifdef MODEL_RECEIVE_WINDOW
	if (_mSrc)
		_mSrc->receivePacket(pkt);
#endif

	pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_RCVDESTROY);

	ts = p->ts();
	p->free();

	// cout << "O seqno" << seqno << " last acked "<< _last_acked;
	if (seqno < _last_acked)
	{
		// cout << "O seqno" << seqno << " last acked "<< _last_acked;
		return;
	}

	// if (_finished)
	// 	return;

	if (seqno == 1)
	{
		// debug:
		// cout << "established" << endl;
		// assert(!_established);
		_established = true;
	}
	else if (seqno > 1 && !_established)
	{
		// cout << "XXX Should be _established " << seqno << endl;
	}

	// assert(seqno >= _last_acked);  // no dups or reordering allowed in this simple simulator

	// compute rtt
	uint64_t m = eventlist().now() - ts;

	if (m != 0)
	{
		if (_rtt > 0)
		{
			uint64_t abs;
			if (m > _rtt)
				abs = m - _rtt;
			else
				abs = _rtt - m;

			_mdev = 3 * _mdev / 4 + abs / 4;
			_rtt = 7 * _rtt / 8 + m / 8;
			_rto = _rtt + 4 * _mdev;
		}
		else
		{
			_rtt = m;
			_mdev = m / 2;
			_rto = _rtt + 4 * _mdev;
		}
		if (_base_rtt == timeInf || _base_rtt > m)
			_base_rtt = m;
	}
	//  cout << "Base "<<timeAsMs(_base_rtt)<< " RTT " << timeAsMs(_rtt)<< " Queued " << queued_packets << endl;

	if (_rto < timeFromMs(10))
    _rto = timeFromMs(10);

	// debug:
	// cerr << this << " hss " << _highest_sent << " seqno = " << seqno << ", _flow_size = " <<  _flow_size << ", _mss = " << _mss << ", packet size = " << pkt.size() << " cwnd " << _cwnd << " ssthresh " << _ssthresh << " time " << eventlist().now() << endl;

	if (seqno >= _flow_size && !_finished)
	{
		_last_acked =
				_finished = true;
		// original:
		// cout << "Flow " << nodename() << " finished at " << timeAsMs(eventlist().now()) << endl;

		// FCT output for processing: (src dst bytes fct_ms timestarted_ms)
		*(fstream_out) << "FCT " << get_flow_src() << " " << get_flow_dst() << " " << get_flowsize() << " " << timeAsMs(eventlist().now() - get_start_time()) << " " << timeAsMs(get_start_time()) << " " << (double)get_flowsize() / timeAsSec(eventlist().now() - get_start_time()) * 8 / 1000000000UL << endl;
		if (application_callback != nullptr)
		{
			application_callback(application_callback_data);
		}
	}

	if (seqno > _last_acked)
	{ // a brand new ack
		if (_old_route)
		{
			if (seqno >= _last_packet_with_old_route)
			{
				// delete _old_route;
				_old_route = NULL;
				// printf("Deleted old route\n");
			}
		}
		_RFC2988_RTO_timeout = eventlist().now() + _rto; // RFC 2988 5.3
		_last_ping = eventlist().now();

		if (seqno >= _highest_sent)
		{
			_highest_sent = seqno;
			_RFC2988_RTO_timeout = timeInf; // RFC 2988 5.2
			_last_ping = timeInf;
		}

#ifdef MODEL_RECEIVE_WINDOW
		int cnt;

		_sent_packets.ack_packet(seqno);

		// if ((cnt = _sent_packets.ack_packet(seqno)) > 2)
		//   cout << "ACK "<<cnt<<" packets on " << _flow.id << " " << _highest_sent+1 << " packets in flight " << _sent_packets.crt_count << " diff " << (_highest_sent+_mss-_last_acked)/1000 << " last_acked " << _last_acked << " at " << timeAsMs(eventlist().now()) << endl;
#endif

		if (!_in_fast_recovery)
		{ // best behaviour: proper ack of a new packet, when we were expecting it
			// clear timers

			_last_acked = seqno;
			_dupacks = 0;
			inflate_window();

			if (_cwnd > _maxcwnd)
			{
				_cwnd = _maxcwnd;
			}

			_unacked = _cwnd;
			_effcwnd = _cwnd;
			if (_logger)
				_logger->logTcp(*this, TcpLogger::TCP_RCV);
			if (!TcpSrc::tcp_flow_paused)
				send_packets();
			else
			{
				tcp_has_pending_send = true;
			}
			return;
		}
		// We're in fast recovery, i.e. one packet has been
		// dropped but we're pretending it's not serious
		if (seqno >= _recoverq)
		{
			// got ACKs for all the "recovery window": resume
			// normal service
			uint32_t flightsize = _highest_sent - seqno;
			_cwnd = min(_ssthresh, flightsize + _mss);
			_unacked = _cwnd;
			_effcwnd = _cwnd;
			_last_acked = seqno;
			_dupacks = 0;
			_in_fast_recovery = false;

			if (_logger)
				_logger->logTcp(*this, TcpLogger::TCP_RCV_FR_END);
			if (!TcpSrc::tcp_flow_paused)
				send_packets();
			else
			{
				tcp_has_pending_send = true;
			}
			return;
		}
		// In fast recovery, and still getting ACKs for the
		// "recovery window"
		// This is dangerous. It means that several packets
		// got lost, not just the one that triggered FR.
		uint32_t new_data = seqno - _last_acked;
		_last_acked = seqno;
		if (new_data < _cwnd)
			_cwnd -= new_data;
		else
			_cwnd = 0;
		_cwnd += _mss;
		if (_logger)
			_logger->logTcp(*this, TcpLogger::TCP_RCV_FR);
		if (!TcpSrc::tcp_flow_paused)
			retransmit_packet();
		else
		{
			tcp_has_pending_retrans = true;
		}
		if (!TcpSrc::tcp_flow_paused)
			send_packets();
		else
		{
			tcp_has_pending_send = true;
		}
		return;
	}
	// It's a dup ack
	if (_in_fast_recovery)
	{ // still in fast recovery; hopefully the prodigal ACK is on it's way
		_cwnd += _mss;
		if (_cwnd > _maxcwnd)
		{
			_cwnd = _maxcwnd;
		}
		// When we restart, the window will be set to
		// min(_ssthresh, flightsize+_mss), so keep track of
		// this
		_unacked = min(_ssthresh, (uint32_t)(_highest_sent - _recoverq + _mss));
		if (_last_acked + _cwnd >= _highest_sent + _mss)
			_effcwnd = _unacked; // starting to send packets again
		if (_logger)
			_logger->logTcp(*this, TcpLogger::TCP_RCV_DUP_FR);
		if (!TcpSrc::tcp_flow_paused)
			send_packets();
		else
		{
			tcp_has_pending_send = true;
		}
		return;
	}
	// Not yet in fast recovery. What should we do instead?
	_dupacks++;

#ifdef PACKET_SCATTER
	if (_dupacks != DUPACK_TH)
#else
	if (_dupacks != 3)
#endif
	{ // not yet serious worry
		if (_logger)
			_logger->logTcp(*this, TcpLogger::TCP_RCV_DUP);
		if (!TcpSrc::tcp_flow_paused)
			send_packets();
		else
		{
			tcp_has_pending_send = true;
		}
		return;
	}
	// _dupacks==3
	if (_last_acked < _recoverq)
	{
		/* See RFC 3782: if we haven't recovered from timeouts
		 etc. don't do fast recovery */
		if (_logger)
			_logger->logTcp(*this, TcpLogger::TCP_RCV_3DUPNOFR);
		return;
	}

	// begin fast recovery

	// only count drops in CA state
	_drops++;

	deflate_window();

	if (_sawtooth > 0)
		_rtt_avg = _rtt_cum / _sawtooth;
	else
		_rtt_avg = timeFromMs(0);

	_sawtooth = 0;
	_rtt_cum = timeFromMs(0);

	if (!TcpSrc::tcp_flow_paused)
		retransmit_packet();
	else
	{
		tcp_has_pending_retrans = true;
	}
	_cwnd = _ssthresh + 3 * _mss;
	_unacked = _ssthresh;
	_effcwnd = 0;
	_in_fast_recovery = true;
	_recoverq = _highest_sent; // _recoverq is the value of the
	// first ACK that tells us things
	// are back on track
	if (_logger)
		_logger->logTcp(*this, TcpLogger::TCP_RCV_DUP_FASTXMIT);
}

void TcpSrc::deflate_window()
{
	if (_mSrc == NULL)
		_ssthresh = max(_cwnd / 2, (uint32_t)(2 * _mss));
	else
		_ssthresh = _mSrc->deflate_window(_cwnd, _mss);
}

void TcpSrc::inflate_window()
{
	int newly_acked = (_last_acked + _cwnd) - _highest_sent;
	// be very conservative - possibly not the best we can do, but
	// the alternative has bad side effects.
	if (newly_acked > _mss)
		newly_acked = _mss;
	if (newly_acked < 0)
		return;
	if (_cwnd < _ssthresh)
	{ // slow start
		int increase = min(_ssthresh - _cwnd, (uint32_t)newly_acked);
		_cwnd += increase;
		newly_acked -= increase;
	}
	else
	{
		// additive increase
		uint32_t pkts = _cwnd / _mss;

		double queued_fraction = 1 - ((double)_base_rtt / _rtt);

		if (queued_fraction >= 0.5 && _mSrc && _cap)
			return;

		if (_mSrc == NULL)
		{
			// int tt = (newly_acked * _mss) % _cwnd;
			_cwnd += (newly_acked * _mss) / _cwnd; // XXX beware large windows, when this increase gets to be very small

			// if (rand()%_cwnd < tt)
			//_cwnd++;
		}
		else
		{
			_cwnd = _mSrc->inflate_window(_cwnd, newly_acked, _mss);
		}

		if (pkts != _cwnd / _mss)
		{
			_rtt_cum += _rtt;
			_sawtooth++;
		}
	}
}

// Note: the data sequence number is the number of Byte1 of the packet, not the last byte.
void TcpSrc::send_packets()
{
	int c = _cwnd;

	if (!_established)
	{
		// send SYN packet and wait for SYN/ACK
		Packet *p = TcpPacket::new_syn_pkt(_flow, *_route, 1, 1);
		_highest_sent = 1;

		p->sendOn();

		if (_RFC2988_RTO_timeout == timeInf)
		{ // RFC2988 5.1
			_RFC2988_RTO_timeout = eventlist().now() + _rto;
		}
		// cerr << this << " src " << _flow_src << " dst " << _flow_dst << " Sending SYN, waiting for SYN/ACK, route sz " << p->_route->size() << endl;
		return;
	}

	if (_app_limited >= 0 && _rtt > 0)
	{
		uint64_t d = (uint64_t)_app_limited * _rtt / 1000000000;
		if (c > d)
		{
			c = d;
		}
		// if (c<1000)
		// c = 1000;

		if (c == 0)
		{
			//      _RFC2988_RTO_timeout = timeInf;
		}

		// rtt in ms
		// printf("%d\n",c);
	}

	while ((_last_acked + c >= _highest_sent + _mss) && (_highest_sent < _flow_size))
	{
		uint64_t data_seq = 0;

#ifdef MODEL_RECEIVE_WINDOW
		int existing_mapping = 0;
		if (_sent_packets.have_mapping(_highest_sent + 1))
		{
			if (!_sent_packets.get_data_seq(_highest_sent + 1, &data_seq))
			{
				cout << "Failed to find TRANSMIT packet on " << _flow.id << " last_acked "
						 << _last_acked + 1 << " highest_sent " << _highest_sent << endl;
				assert(0);
			}
			else
			{
				existing_mapping = 1;
			}
		}
		else
		{
			if (_mSrc && !_mSrc->getDataSeq(&data_seq, this))
				break;
		}

		if (!existing_mapping)
			_sent_packets.add_packet(_highest_sent + 1, data_seq);

		if (data_seq > _highest_data_seq)
			_highest_data_seq = data_seq;

			//      cout << "Transmit packet on " << _flow.id << " " << _highest_sent+1 << "[" << data_seq << "] packets in flight " << _sent_packets.crt_count << " diff " << (_highest_sent+_mss-_last_acked)/1000 << " last_acked " << _last_acked << " at " << timeAsMs(eventlist().now()) << endl;
#endif

#ifdef PACKET_SCATTER
		TcpPacket *p;

		if (_paths)
		{

#ifdef RANDOM_PATH
			_crt_path = random() % _paths->size();
#endif

			p = TcpPacket::newpkt(_flow, *(_paths->at(_crt_path)), _highest_sent + 1,
														data_seq, _mss);
			_crt_path = (_crt_path + 1) % _paths->size();
		}
		else
		{
			p = TcpPacket::newpkt(_flow, *_route, _highest_sent + 1, data_seq, _mss);
		}
#else
		TcpPacket *p = TcpPacket::newpkt(_flow, *_route, _highest_sent + 1, data_seq, _mss);
#endif
		p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);
		p->set_ts(eventlist().now());

		_highest_sent += _mss; // XX beware wrapping
		_packets_sent += _mss;

		p->sendOn();
		// cout << "Transmit packet on " << _flow.id << " " << _highest_sent+1 << "[" << p->size() << "] " << " diff " << (_highest_sent+_mss-_last_acked)/1000 << " last_acked " << _last_acked << " at " << timeAsMs(eventlist().now()) << endl;

		if (_RFC2988_RTO_timeout == timeInf)
		{ // RFC2988 5.1
			_RFC2988_RTO_timeout = eventlist().now() + _rto;
		}
	}
}

void TcpSrc::retransmit_packet()
{
	if (!_established)
	{
		// std::cerr << "hss " << _highest_sent << endl;
		assert(_highest_sent == 1);

		Packet *p = TcpPacket::new_syn_pkt(_flow, *_route, 1, 1);
		p->sendOn();

		// cerr << "Resending SYN, waiting for SYN/ACK" << endl;
		return;
	}

	uint64_t data_seq = 0;

#ifdef MODEL_RECEIVE_WINDOW
	if (!_sent_packets.get_data_seq(_last_acked + 1, &data_seq))
	{
		cout << "Failed to find packet on " << _flow.id << " last_acked " << _last_acked + 1 << " highest_sent " << _highest_sent << endl;
		assert(NULL);
	}
	//  else
	//  cout << "Retransmit packet on " << _flow.id << " " << _last_acked+1 << " " << data_seq << endl;
#endif

#ifdef PACKET_SCATTER
	TcpPacket *p;
	if (_paths)
	{

#ifdef RANDOM_PATH
		_crt_path = random() % _paths->size();
#endif

		p = TcpPacket::newpkt(_flow, *(_paths->at(_crt_path)), _last_acked + 1, data_seq, _mss);
		_crt_path = (_crt_path + 1) % _paths->size();
	}
	else
	{
		p = TcpPacket::newpkt(_flow, *_route, _last_acked + 1, _mss);
	}
#else
	TcpPacket *p = TcpPacket::newpkt(_flow, *_route, _last_acked + 1, data_seq, _mss);
#endif

	p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);
	p->set_ts(eventlist().now());
	p->sendOn();
	//  cerr << "Retransmit packet on " << _flow.id << " " << _last_acked+1 << " " << data_seq << endl;

	_packets_sent += _mss;

	if (_RFC2988_RTO_timeout == timeInf)
	{ // RFC2988 5.1
		_RFC2988_RTO_timeout = eventlist().now() + _rto;
	}
}

void TcpSrc::rtx_timer_hook(simtime_picosec now, simtime_picosec period)
{
	if (now <= _RFC2988_RTO_timeout || _RFC2988_RTO_timeout == timeInf)
		return;

	if (_highest_sent == 0)
		return;

	  cout <<"At " << now/(double)1000000000<< " RTO " << _rto/1000000000 << " MDEV "
	 << _mdev/1000000000 << " RTT "<< _rtt/1000000000 << " SEQ " << _last_acked / _mss << " HSENT "  << _highest_sent
	 << " CWND "<< _cwnd/_mss << " FAST RECOVERY? " << 	_in_fast_recovery << " Flow ID "
	 << str() << " SRC " << _flow_src << " DST " << _flow_dst << endl;

	// here we can run into phase effects because the timer is checked
	// only periodically for ALL flows but if we keep the difference
	// between scanning time and real timeout time when restarting the
	// flows we should minimize them !
	if (!_rtx_timeout_pending)
	{
		_rtx_timeout_pending = true;

		// check the timer difference between the event and the real value
		simtime_picosec too_late = now - (_RFC2988_RTO_timeout);

		// careful: we might calculate a negative value if _rto suddenly drops very much
		// to prevent overflow but keep randomness we just divide until we are within the limit
		while (too_late > period)
			too_late >>= 1;

		// carry over the difference for restarting
		simtime_picosec rtx_off = (period - too_late) / 200;

		eventlist().sourceIsPendingRel(*this, rtx_off);

		// reset our rtx timerRFC 2988 5.5 & 5.6

		_rto *= 2;
		// if (_rto > timeFromMs(1000))
		//   _rto = timeFromMs(1000);
		_RFC2988_RTO_timeout = now + _rto;
	}
}

void TcpSrc::doNextEvent()
{
	if (_rtx_timeout_pending)
	{
		_rtx_timeout_pending = false;

		if (_logger)
			_logger->logTcp(*this, TcpLogger::TCP_TIMEOUT);

		if (_in_fast_recovery)
		{
			uint32_t flightsize = _highest_sent - _last_acked;
			_cwnd = min(_ssthresh, flightsize + _mss);
		}

		deflate_window();

		_cwnd = _mss;

		_unacked = _cwnd;
		_effcwnd = _cwnd;
		_in_fast_recovery = false;
		_recoverq = _highest_sent;

		if (_established)
			_highest_sent = _last_acked + _mss;

		_dupacks = 0;

		if (!TcpSrc::tcp_flow_paused)
			retransmit_packet();
		else
		{
			tcp_has_pending_retrans = true;
		}

		if (_sawtooth > 0)
			_rtt_avg = _rtt_cum / _sawtooth;
		else
			_rtt_avg = timeFromMs(0);

		_sawtooth = 0;
		_rtt_cum = timeFromMs(0);

		if (_mSrc)
			_mSrc->window_changed();
	}
	else
	{
		// cout << "Starting flow" << endl;
		startflow();
	}
}

void TcpSrc::pause_flow()
{
	TcpSrc::tcp_flow_paused = true;
	// std::cerr << "PAUSE " << get_flow_src() << " " << get_flow_dst() << " total: " << get_flowsize() << " left: " << get_flowsize() - _highest_sent << " " << std::endl;
}

void TcpSrc::resume_all_flow() 
{
	TcpSrc::tcp_flow_paused = false;
}

void TcpSrc::resume_flow()
{
	// std::cerr << "RESUME " << get_flow_src() << " " << get_flow_dst() << " total: " << get_flowsize() << " left: " << (int64_t)get_flowsize() - _highest_sent << " " << std::endl;
	
	// if (_highest_sent == 0)
	if (tcp_has_pending_send)
	{
		// std::cerr << "send called " << std::endl;
		// std::cerr << this << " hss = " << _highest_sent << " lack " << _last_acked << " est " << _established << ", _flow_size = " <<  _flow_size << ", _mss = " << _mss << " cwnd " <<  _cwnd << " ssthresh " << _ssthresh << " time " << eventlist().now() << endl;
		send_packets();
		tcp_has_pending_send = false;
	}
	// else 
	if (tcp_has_pending_retrans)
	{
		// std::cerr << "Retr called " << std::endl;
		// std::cerr << this << " hss = " << _highest_sent << " lack " << _last_acked << " est " << _established << ", _flow_size = " <<  _flow_size << ", _mss = " << _mss << " cwnd " << _cwnd << " ssthresh " << _ssthresh << " time " << eventlist().now() << endl;
		retransmit_packet();
		tcp_has_pending_retrans = false;
	}
}

void TcpSrc::update_route(Route *routeout, Route *routein)
{
	// replace_route(routeout);
	// std::cerr << "src " << _flow_src << " dst " << _flow_dst << "prev route size " << _route->size() << " new route size " << routeout->size() << endl;
	delete _route;
	_route = routeout;
	delete _sink->_route;
	_sink->_route = routein;
}

////////////////////////////////////////////////////////////////
//  TCP SINK
////////////////////////////////////////////////////////////////

TcpSink::TcpSink()
		: Logged("sink"), _cumulative_ack(0), _packets(0), _mSink(0), _crt_path(0)
{
	_nodename = "tcpsink";
}

TcpSink::~TcpSink()
{
	if (_route)
	{
		delete _route;
	}
#ifdef PACKET_SCATTER
	if (_paths)
	{
		for (Route *r : _paths)
		{
			delete r;
		}
		delete _paths;
	}
#endif
}

void TcpSink::connect(TcpSrc &src, const Route &route)
{
	_src = &src;
	_route = &route;
	_cumulative_ack = 0;
	_drops = 0;
}

// Note: _cumulative_ack is the last byte we've ACKed.
// seqno is the first byte of the new packet.
void TcpSink::receivePacket(Packet &pkt)
{
	TcpPacket *p = (TcpPacket *)(&pkt);
	TcpPacket::seq_t seqno = p->seqno();
	simtime_picosec ts = p->ts();

	bool marked = p->flags() & ECN_CE;

	if (_mSink != NULL)
	{
		_mSink->receivePacket(pkt);
	}

	int size = p->size(); // TODO: the following code assumes all packets are the same size
	pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_RCVDESTROY);
	p->free();

	_packets += p->size();

	// std::cerr << this << " Sink: received seqno " << seqno << " size " << size << " time " << _src->eventlist().now() << endl;

	if (seqno == _cumulative_ack + 1)
	{ // it's the next expected seq no
		_cumulative_ack = seqno + size - 1;
		// if (_src->demand_recorder)
		// 	_src->demand_recorder->satisfied(_src->_flow_src, _src->_flow_dst, size);
		// cout << "New cumulative ack is " << _cumulative_ack << endl;
		// are there any additional received packets we can now ack?
		while (!_received.empty() && (_received.front() == _cumulative_ack + 1))
		{
			_received.pop_front();
			_cumulative_ack += size;
			// if (_src->demand_recorder)
			// _src->demand_recorder->satisfied(_src->_flow_src, _src->_flow_dst, size);
		}
	}
	else if (seqno < _cumulative_ack + 1)
	{
	}
	else
	{ // it's not the next expected sequence number
		if (_received.empty())
		{
			_received.push_front(seqno);
			// it's a drop in this simulator there are no reorderings.
			_drops += (1000 + seqno - _cumulative_ack - 1) / 1000;
		}
		else if (seqno > _received.back())
		{ // likely case
			_received.push_back(seqno);
		}
		else
		{ // uncommon case - it fills a hole
			list<uint64_t>::iterator i;
			for (i = _received.begin(); i != _received.end(); i++)
			{
				if (seqno == *i)
					break; // it's a bad retransmit
				if (seqno < (*i))
				{
					_received.insert(i, seqno);
					break;
				}
			}
		}
	}
	send_ack(ts, marked);
}

void TcpSink::send_ack(simtime_picosec ts, bool marked)
{

	// debug:
	// cout << "Sink: sending an ACK" << endl;

	const Route *rt = _route;

#ifdef PACKET_SCATTER
	if (_paths)
	{
#ifdef RANDOM_PATH
		_crt_path = random() % _paths->size();
#endif

		rt = _paths->at(_crt_path);
		_crt_path = (_crt_path + 1) % _paths->size();
	}
#endif

	TcpAck *ack = TcpAck::newpkt(_src->_flow, *rt, 0, _cumulative_ack,
															 _mSink != NULL ? _mSink->data_ack() : 0);

	ack->flow().logTraffic(*ack, *this, TrafficLogger::PKT_CREATESEND);
	ack->set_ts(ts);
	if (marked)
		ack->set_flags(ECN_ECHO);
	else
		ack->set_flags(0);
	_src->receivePacket(*ack);
	// cerr << "Transmit ack on " << _src->_flow.id << " " << _cumulative_ack << endl;
	// ack->sendOn();
}

#ifdef PACKET_SCATTER
void TcpSink::set_paths(vector<const Route *> *rt)
{
	// this should only be used with route
	_paths = new vector<const Route *>();

	for (unsigned int i = 0; i < rt->size(); i++)
	{
		Route *t = new Route(*(rt->at(i)));
		t->push_back(_src);
		_paths->push_back(t);
	}
}
#endif

////////////////////////////////////////////////////////////////
//  TCP RETRANSMISSION TIMER
////////////////////////////////////////////////////////////////

TcpRtxTimerScanner::TcpRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist)
		: EventSource(eventlist, "RtxScanner"), _scanPeriod(scanPeriod)
{
	eventlist.sourceIsPendingRel(*this, _scanPeriod);
}

void TcpRtxTimerScanner::registerTcp(TcpSrc &tcpsrc)
{
	_tcps.push_back(&tcpsrc);
}

void TcpRtxTimerScanner::doNextEvent()
{
	simtime_picosec now = eventlist().now();
	tcps_t::iterator i = _tcps.begin();
	while (i != _tcps.end())
	{
		if ((*i)->_finished)
		{
			eventlist().cancelPendingSource(**i);
			// delete *i;
			i = _tcps.erase(i);
		}
		else
		{
			(*i)->rtx_timer_hook(now, _scanPeriod);
			i++;
		}
	}
	// for (i = _tcps.begin(); i!=_tcps.end(); i++) {
	// 	// just take care of the trash omg...
	// 	if ((*i)->_finished) {
	// 		eventlist().cancelPendingSource(**i);
	// 		_tcps.erase(i);
	// 		delete *i;
	// 	} else {
	// 		(*i)->rtx_timer_hook(now,_scanPeriod);
	// 	}
	// }
	eventlist().sourceIsPendingRel(*this, _scanPeriod);
}
