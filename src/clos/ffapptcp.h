#ifndef FF_APP_TCP_H
#define FF_APP_TCP_H

#include "loggers.h"
#undef max 

#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "topology.h"
#include "eventlist.h"
#include "ndp.h"

/*
 * An application that takes a Flex-flow generated task graph
 * and simulates it on top of the opera network
 */
class FFTaskTCP;

class FFApplicationTCP {
public:
  FFApplicationTCP(Topology* top, int ss, TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl,
    TcpRtxTimerScanner & rtx, EventList & eventlist, std::string taskgraph);
  ~FFApplicationTCP();

  void start_init_tasks();

  int ssthresh;
  double pull_rate;
  std::unordered_map<int, FFTaskTCP*> tasks;
  Topology * topology; 
  EventList & eventlist;
  TcpSinkLoggerSampling & sinkLogger;
  TcpTrafficLogger & tcpTrafficLogger;
  TcpRtxTimerScanner & tcpRtxScanner;
};

class FFTaskTCP : public EventSource {
public:
  enum FFTaskTypeTCP {FF_COMM, FF_COMP, FF_INTRA_COMM};

  // FFTaskTCP(FFTaskTypeTCP type, EventList & eventlist,
  //        float rTime, float sTime, float cTime, float xfsz, 
  //        int wid, int gid, int fworker, int tworker, int fGuid, int tGuid);
  FFTaskTCP(FFApplicationTCP * ffapp, FFTaskTypeTCP type, EventList & eventlist);

  void add_pretask(FFTaskTCP * task);
  void add_nextask(FFTaskTCP * task);

  void taskstart();
  void cleanup();
  void start_flow();

  virtual void doNextEvent(); // call task event

  FFApplicationTCP * ffapp;
  FFTaskTypeTCP type;
  float readyTime, startTime, computeTime, xferSize;
  int workerId, guid, fromWorker, toWorker, fromGuid, toGuid, fromNode, toNode;
  simtime_picosec sim_start, sim_finish, sim_duration;
  bool started;
  std::unordered_set<FFTaskTCP*> preTasks; 
  std::vector<FFTaskTCP*> nextTasks;
};

void fftcptaskfinish(void * task);

#endif // FF_APP_TCP_H

