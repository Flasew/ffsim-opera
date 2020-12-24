#ifndef FC_TOP
#define FC_TOP
#include "main.h"
#include "randomqueue.h"
#include "pipe.h"
#include "config.h"
#include "loggers.h"
#include "network.h"
#include "firstfit.h"
#include "topology.h"
#include "logfile.h"
#include "eventlist.h"
#include "switch.h"
#include <ostream>
#include <unordered_map>

#ifndef QT
#define QT
typedef enum {RANDOM, ECN, COMPOSITE, CTRL_PRIO, LOSSLESS, LOSSLESS_INPUT, LOSSLESS_INPUT_ECN} queue_type;
#endif
using namespace std;
class FlatTopology: public Topology {

public:

  vector<Switch*> switchs;

  vector<vector<Pipe*>> pipes;
  vector<vector<Queue*>> queues;
  
  FirstFit* ff;
  Logfile* logfile;
  EventList* eventlist;
  int failed_links;
  int _no_of_nodes;
  queue_type qt;

  FlatTopology(int no_of_nodes, mem_b queuesize, Logfile* log, EventList* ev, 
               FirstFit* f, queue_type q);

  void init_network();
  void load_topology_protobuf(std::string & taskgraph);
  virtual vector<const Route*>* get_paths(int src, int dest);

  Pipe * get_pipe(int src, int dst) { return pipes[src][dst]; };
  Queue* alloc_src_queue(QueueLogger* q);
  Queue* alloc_queue(QueueLogger* q, mem_b queuesize);
  Queue* alloc_queue(QueueLogger* q, uint64_t speed, mem_b queuesize);

  void count_queue(Queue*);
  void print_path(std::ofstream& paths,int src,const Route* route);
  vector<int>* get_neighbours(int src) { return NULL;};
  int no_of_nodes() const {return _no_of_nodes;}
  int find_lp_switch(Queue* queue);

  uint32_t _link_speed;
  unordered_map<uint64_t, size_t> _conn_list;
  unordered_map<uint64_t, vector<size_t>*> _routes;
private:
  map<Queue*,int> _link_usage;

  int find_destination(Queue* queue);
  void set_params(int no_of_nodes);
  mem_b _queuesize;
};

class UtilMonitor : public EventSource {
 public:

    UtilMonitor(FlatTopology* top, EventList &eventlist);

    void start(simtime_picosec period);
    void doNextEvent();
    void printAggUtil();

    FlatTopology* _top;
    simtime_picosec _period; // picoseconds between utilization reports
    int64_t _max_agg_Bps; // delivered to endhosts, across the whole network
    int64_t _max_B_in_period;
    int _H; // number of hosts

};



#endif
