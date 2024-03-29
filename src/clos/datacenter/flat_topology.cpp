// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "flat_topology.h"
#include <vector>
#include "string.h"
#include <sstream>
#include <fstream>
#include <strstream>
#include <iostream>
// #include "taskgraph.pb.h"

#include "main.h"
#include "queue.h"
#include "switch.h"
#include "compositequeue.h"
#include "prioqueue.h"
#include "queue_lossless.h"
#include "queue_lossless_input.h"
#include "queue_lossless_output.h"
#include "ecnqueue.h"

#include "taskgraph_generated.h"

#define EDGE(a, b, n) ((a) > (b) ? ((a) * (n) + (b)) : ((b) * (n) + (a)))

extern uint32_t RTT;
extern uint32_t SPEED;
extern ofstream fct_util_out;

string ntoa(double n);
string itoa(uint64_t n);

//extern int N;
static bool LoadFileRaw(const char *name, std::string *buf)
{
  std::ifstream ifs(name, std::ifstream::binary);
  if (!ifs.is_open())
  {
    return false;
  }
  // The fastest way to read a file into a string.
  ifs.seekg(0, std::ios::end);
  auto size = ifs.tellg();
  (*buf).resize(static_cast<size_t>(size));
  ifs.seekg(0, std::ios::beg);
  ifs.read(&(*buf)[0], (*buf).size());
  return !ifs.bad();
}

FlatTopology::FlatTopology(int no_of_nodes, const string &tgfile, mem_b queuesize, Logfile *lg, EventList *ev, FirstFit *fit, queue_type q)
{
  _queuesize = queuesize;
  logfile = lg;
  eventlist = ev;
  ff = fit;
  qt = q;
  failed_links = 0;

  set_params(no_of_nodes);
  // load_topology_protobuf(tgfile);
  load_topology_flatbuf(tgfile);

  init_network();
}

void FlatTopology::set_params(int no_of_nodes)
{
  cout << "Set params " << no_of_nodes << endl;

  _no_of_nodes = no_of_nodes;

  switchs.resize(_no_of_nodes, nullptr);
  pipes.resize(_no_of_nodes, vector<Pipe *>(_no_of_nodes));
  queues.resize(_no_of_nodes, vector<Queue *>(_no_of_nodes));
}

FlatTopology::FlatTopology(int no_of_nodes, mem_b queuesize, Logfile *lg, EventList *ev, FirstFit *fit, queue_type q)
{
  _queuesize = queuesize;
  logfile = lg;
  eventlist = ev;
  ff = fit;
  qt = q;
  failed_links = 0;

  set_params(no_of_nodes);
  // load_topology_protobuf(tgfile);
  // load_topology_flatbuf(tgfile);
  for (int i = 0; i < _no_of_nodes; i++)
  {
    for (int j = 0; j < _no_of_nodes; j++)
    {
      if (i != j)
      {
        _conn_list[EDGE(i, j, _no_of_nodes)] = 1;
        uint64_t route_id = i * _no_of_nodes + j;
        _routes[route_id] = vector<vector<size_t> *>();
        vector<size_t> *path_vector = new vector<size_t>();
        path_vector->push_back(i);
        path_vector->push_back(j);
        _routes[route_id].push_back(path_vector);
      }
    }
  }
  init_network();
}

void FlatTopology::load_topology_flatbuf(const std::string &taskgraph)
{
  string buffer;
  bool success = LoadFileRaw(taskgraph.c_str(), &buffer);
  if (!success)
  {
    assert("Failed to read file!" && false);
  }
  auto fbuf_tg = flatbuffers::GetRoot<FlatBufTaskGraph::TaskGraph>(buffer.c_str());
  for (int i = 0; i < fbuf_tg->conn()->size(); i++)
  {
    auto conn = fbuf_tg->conn()->Get(i);
    _conn_list[EDGE(conn->fromnode(), conn->tonode(), _no_of_nodes)] = conn->nconn();
  }

  for (int i = 0; i < fbuf_tg->routes()->size(); i++)
  {
    auto route = fbuf_tg->routes()->Get(i);
    uint64_t route_id = route->fromnode() * _no_of_nodes + route->tonode();
    cerr << "adding " << route->fromnode() << "->" << route->tonode() << " id " << route_id << endl;

    for (int j = 0; j < route->paths()->size(); j++)
    {
      auto path = route->paths()->Get(j);
      vector<size_t> *path_vector = new vector<size_t>();
      for (int k = 0; k < path->hopnode()->size(); k++)
      {
        path_vector->push_back(path->hopnode()->Get(k));
        cerr << path->hopnode()->Get(k) << ", ";
      }
      if (_routes.find(route_id) == _routes.end())
      {
        _routes[route_id] = vector<vector<size_t> *>();
      }
      _routes[route_id].push_back(path_vector);
      cerr << endl;
    }
  }
}

#if 0
void FlatTopology::load_topology_protobuf(const std::string & taskgraph) {
    TaskGraphProtoBuf::TaskGraph tg;
    std::fstream input(taskgraph, std::ios::in | std::ios::binary);
    if (!tg.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse taskgraph." << std::endl;
        assert(false);
    } 

    for (int i = 0; i < tg.conn_size(); i++) {
        const TaskGraphProtoBuf::Connection& conn = tg.conn(i);
        _conn_list[EDGE(conn.from(), conn.to(), _no_of_nodes)] = conn.nconn();
    }

    for (int i = 0; i < tg.routes_size(); i++) {

        const TaskGraphProtoBuf::Route& route = tg.routes(i);
        uint64_t route_id = route.from() * _no_of_nodes + route.to();
        // cerr << "adding " << route.from() << "->" <<  route.to() << "id" << route_id << endl;
        
        for (int j = 0; j < route.paths_size(); j++) {
            const TaskGraphProtoBuf::Path& path = route.paths(j);
            vector<size_t>* path_vector = new vector<size_t>();
            for (int k = 0; k < path.hopnode_size(); k++) {
                path_vector->push_back(path.hopnode(k));
                // cerr << path.hopnode(k) << ", ";
            }
            if (_routes.find(route_id) == _routes.end()) {
                _routes[route_id] = vector<vector<size_t>* >();
            }
            _routes[route_id].push_back(path_vector);
            // cerr << endl;

        }
    }
}
#endif
// Queue* FlatTopology::alloc_src_queue(QueueLogger* queueLogger){
//     return  new PriorityQueue(speedFromMbps((uint64_t)SPEED), memFromPkt(FEEDER_BUFFER), *eventlist, queueLogger);
// }

// Queue* FlatTopology::alloc_queue(QueueLogger* queueLogger, mem_b queuesize){
//     return alloc_queue(queueLogger, SPEED, queuesize);
// }

Queue *FlatTopology::alloc_queue(QueueLogger *queueLogger, uint64_t speed, mem_b queuesize)
{
  if (qt == RANDOM)
    return new RandomQueue(speedFromMbps(speed), memFromPkt(SWITCH_BUFFER + RANDOM_BUFFER), *eventlist, queueLogger, memFromPkt(RANDOM_BUFFER));
  else if (qt == COMPOSITE)
    return new CompositeQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger);
  else if (qt == CTRL_PRIO)
    return new CtrlPrioQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger);
  else if (qt == ECN)
    return new ECNQueue(speedFromMbps(speed), memFromPkt(queuesize), *eventlist, queueLogger, memFromPkt(50));
  else if (qt == LOSSLESS)
    return new LosslessQueue(speedFromMbps(speed), memFromPkt(50), *eventlist, queueLogger, NULL);
  else if (qt == LOSSLESS_INPUT)
    return new LosslessOutputQueue(speedFromMbps(speed), memFromPkt(200), *eventlist, queueLogger);
  else if (qt == LOSSLESS_INPUT_ECN)
    return new LosslessOutputQueue(speedFromMbps(speed), memFromPkt(10000), *eventlist, queueLogger, 1, memFromPkt(16));
  assert(0);
}

void FlatTopology::init_network()
{
  QueueLoggerSampling *queueLogger = nullptr;

  for (int j = 0; j < _no_of_nodes; j++)
    for (int k = 0; k < _no_of_nodes; k++)
    {
      queues[j][k] = nullptr;
      pipes[j][k] = nullptr;
    }

  //create switches if we have lossless operation
  if (qt == LOSSLESS)
    for (int j = 0; j < _no_of_nodes; j++)
    {
      switchs[j] = new Switch("Switch_LowerPod_" + ntoa(j));
    }

  for (int j = 0; j < _no_of_nodes; j++)
  {
    for (int k = 0; k < j; k++)
    {
      if (_conn_list.find(EDGE(j, k, _no_of_nodes)) != _conn_list.end())
      {
        // QueueLoggerSampling* queueLoggerd = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
        // QueueLoggerSampling* queueLoggeru = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
        // queueLogger = NULL;
        // logfile->addLogger(*queueLoggerd);
        // logfile->addLogger(*queueLoggeru);

        queues[j][k] = alloc_queue(queueLogger, SPEED * _conn_list[EDGE(j, k, _no_of_nodes)], _queuesize);
        cerr << "(" << j << ", " << k << ")" << SPEED * _conn_list[EDGE(j, k, _no_of_nodes)] << endl;
        queues[k][j] = alloc_queue(queueLogger, SPEED * _conn_list[EDGE(j, k, _no_of_nodes)], _queuesize);
        queues[j][k]->setName("L" + ntoa(j) + "->DST" + ntoa(k));
        queues[k][j]->setName("L" + ntoa(k) + "->DST" + ntoa(j));
        // logfile->writeName(*(queues[j][k]));
        // logfile->writeName(*(queues[k][j]));

        pipes[j][k] = new Pipe(timeFromNs(RTT), *eventlist);
        pipes[k][j] = new Pipe(timeFromNs(RTT), *eventlist);
        pipes[j][k]->setName("Pipe-LS" + ntoa(j) + "->DST" + ntoa(k));
        pipes[k][j]->setName("Pipe-LS" + ntoa(k) + "->DST" + ntoa(j));
        // logfile->writeName(*(pipes[j][k]));
        // logfile->writeName(*(pipes[k][j]));

        if (qt == LOSSLESS)
        {
          switchs[j]->addPort(queues[j][k]);
          ((LosslessQueue *)queues[j][k])->setRemoteEndpoint(queues[k][j]);
          switchs[k]->addPort(queues[k][j]);
          ((LosslessQueue *)queues[k][j])->setRemoteEndpoint(queues[j][k]);
        }
        else if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN)
        {
          //no virtual queue needed at server
          new LosslessInputQueue(*eventlist, queues[k][j]);
          new LosslessInputQueue(*eventlist, queues[j][k]);
        }

        if (ff)
        {
          ff->add_queue(queues[j][k]);
          ff->add_queue(queues[k][j]);
        }
      }
    }
  }

  //init thresholds for lossless operation
  if (qt == LOSSLESS)
    for (int j = 0; j < _queuesize; j++)
    {
      switchs[j]->configureLossless();
    }
}

// ???
void check_non_null(Route *rt)
{
  int fail = 0;
  for (unsigned int i = 1; i < rt->size() - 1; i += 2)
    if (rt->at(i) == NULL)
    {
      fail = 1;
      break;
    }

  if (fail)
  {
    //    cout <<"Null queue in route"<<endl;
    for (unsigned int i = 1; i < rt->size() - 1; i += 2)
      printf("%p ", rt->at(i));

    cout << endl;
    assert(0);
  }
}

// single route assumption for now.
// in any case unless we ECMP this should be fixed.
vector<const Route *> *FlatTopology::get_paths(int src, int dest)
{
  vector<const Route *> *paths = new vector<const Route *>();

  route_t *routeout, *routeback;

  // NOTE: HARD CODED `0` BECAUSE THERE'S ONLY ONE SWITCH
  // cerr << "id: " << src * _no_of_nodes + dest << endl;
  assert(_routes.find(src * _no_of_nodes + dest) != _routes.end());

  for (const vector<size_t> *r : _routes[src * _no_of_nodes + dest])
  {
    // forward path
    routeout = new Route();
    //routeout->push_back(pqueue);

    const vector<size_t> &route = *r;

    for (size_t i = 0; i < route.size() - 1; i++)
    {
      assert(queues[route[i]][route[i + 1]] != nullptr);
      routeout->push_back(queues[route[i]][route[i + 1]]);
      routeout->push_back(pipes[route[i]][route[i + 1]]);
    }
    // routeout->push_back(queues[src][dest]);
    // routeout->push_back(pipes[src][dest]);

    if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN)
      routeout->push_back(queues[src][dest]->getRemoteEndpoint());

    routeback = new Route();
    // reverse path for RTS packets
    // assert(_routes.find(dest * _no_of_nodes + src) != _routes.end());
    // route = *(_routes[dest * _no_of_nodes + src]);

    for (size_t i = route.size() - 1; i > 1; i--)
    {
      assert(queues[route[i]][route[i - 1]] != nullptr);
      routeback->push_back(queues[route[i]][route[i - 1]]);
      routeback->push_back(pipes[route[i]][route[i - 1]]);
    }

    // routeback->push_back(queues[dest][src]);
    // routeback->push_back(pipes[dest][src]);

    if (qt == LOSSLESS_INPUT || qt == LOSSLESS_INPUT_ECN)
      routeback->push_back(queues[dest][src]->getRemoteEndpoint());

    routeout->set_reverse(routeback);
    routeback->set_reverse(routeout);

    // std::cerr << "get_paths: src " << src << " dst " << dest << std::endl;
    // print_route(*routeout);
    paths->push_back(routeout);

    check_non_null(routeout);
  }
  return paths;
}

void FlatTopology::count_queue(Queue *queue)
{
  if (_link_usage.find(queue) == _link_usage.end())
  {
    _link_usage[queue] = 0;
  }

  _link_usage[queue] = _link_usage[queue] + 1;
}

// Find lower pod switch:
int FlatTopology::find_lp_switch(Queue *queue)
{
  //first check ns_nlp
  for (int i = 0; i < _no_of_nodes; i++)
    for (int j = 0; j < _no_of_nodes; j++)
      if (queues[i][j] == queue)
        return j;

  //only count nup to nlp
  count_queue(queue);

  return -1;
}

int FlatTopology::find_destination(Queue *queue)
{
  //first check nlp_ns
  for (int i = 0; i < _no_of_nodes; i++)
    for (int j = 0; j < _no_of_nodes; j++)
      if (queues[i][j] == queue)
        return j;

  return -1;
}

void FlatTopology::print_path(std::ofstream &paths, int src, const Route *route)
{
  paths << "SRC_" << src << " ";

  if (route->size() / 2 == 2)
  {
    paths << "LS_" << find_lp_switch((Queue *)route->at(1)) << " ";
    paths << "DST_" << find_destination((Queue *)route->at(3)) << " ";
  }
  else
  {
    paths << "Wrong hop count " << ntoa(route->size() / 2);
  }

  paths << endl;
}

// UtilMonitor::UtilMonitor(FlatTopology* top, EventList &eventlist)
//   : EventSource(eventlist,"utilmonitor"), _top(top)
// {
//     _H = _top->no_of_nodes(); // number of hosts
//     uint64_t rate = 10000000000 / 8; // bytes / second
//     rate = rate * _H * _H;

//     _max_agg_Bps = rate;

//     // debug:
//     //cout << "max packets per second = " << rate << endl;

// }

// void UtilMonitor::start(simtime_picosec period) {
//     _period = period;
//     _max_B_in_period = _max_agg_Bps * timeAsSec(_period);

//     // debug:
//     //cout << "_max_pkts_in_period = " << _max_pkts_in_period << endl;

//     eventlist().sourceIsPending(*this, _period);
// }

// void UtilMonitor::doNextEvent() {
//     printAggUtil();
// }

// void UtilMonitor::printAggUtil() {

//     uint64_t B_sum = 0;

//     // int host = 0;
//     // for (int tor = 0; tor < _N; tor++) {
//     //     for (int downlink = 0; downlink < _hpr; downlink++) {
//     //         Pipe* pipe = _top->get_downlink(tor, host);
//     //         B_sum = B_sum + pipe->reportBytes();
//     //         host++;
//     //     }
//     // }
//     for (int i = 0; i < _H; i++) {
//       for (int j = 0; j < _H; j++) {
//         Pipe * pipe = _top->get_pipe(i, j);
//         if (pipe != nullptr) {
//           B_sum = B_sum + pipe->reportBytes();
//         }
//       }
//     }

//     // debug:
//     //cout << "Bsum = " << B_sum << endl;
//     //cout << "_max_B_in_period = " << _max_B_in_period << endl;

//     double util = (double)B_sum / (double)_max_B_in_period;

//     fct_util_out << "Util " << fixed << util << " " << timeAsMs(eventlist().now()) << endl;

//     //if (eventlist().now() + _period < eventlist().getEndtime())
//     eventlist().sourceIsPendingRel(*this, _period);

// }
