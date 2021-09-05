// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "config.h"
#include <sstream>
#include <strstream>
#include <fstream> // need to read flows
#include <iostream>
#include <string.h>
#include <math.h>
#include "network.h"
#include "randomqueue.h"
//#include "shortflows.h"
#include "pipe.h"
#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "clock.h"
#include "tcp.h"
#include "mtcp.h"
#include "compositequeue.h"
#include "firstfit.h"
#include "topology.h"
#include "dyn_net_sch.h"
//#include "connection_matrix.h"

// Choose the topology here:
// #include "test_topology.h"
#include "flat_topology.h"
#include "ffapp.h"

#include <list>

// Simulation params

#define PRINT_PATHS 0

#define PERIODIC 0
#include "main.h"

uint32_t RTT = 1000; // ns
int DEFAULT_NODES = 16;

uint32_t SPEED;
std::ofstream fct_util_out;

FirstFit *ff = NULL;
//unsigned int subflow_count = 8; // probably not necessary ???

#define DEFAULT_PACKET_SIZE 9000 // full packet (including header), Bytes
#define DEFAULT_HEADER_SIZE 64   // header size, Bytes
#define DEFAULT_QUEUE_SIZE 100
#define DEFAULT_DEGREE 4
#define DEFAULT_RECONF_DELAY 100

string ntoa(double n);
string itoa(uint64_t n);

EventList eventlist;
// Logfile* lg;

void exit_error(char *progr, char *param)
{
  cerr << "Bad parameter: " << param << endl;
  cerr << "Usage " << progr << " [UNCOUPLED(DEFAULT)|COUPLED_INC|FULLY_COUPLED|COUPLED_EPSILON] [epsilon][COUPLED_SCALABLE_TCP]" << endl;
  exit(1);
}

void print_path(std::ofstream &paths, const Route *rt)
{
  for (unsigned int i = 1; i < rt->size() - 1; i += 2)
  {
    RandomQueue *q = (RandomQueue *)rt->at(i);
    if (q != NULL)
      paths << q->str() << " ";
    else
      paths << "NULL ";
  }

  paths << endl;
}

int main(int argc, char **argv)
{

  TcpPacket::set_packet_size(DEFAULT_PACKET_SIZE - DEFAULT_HEADER_SIZE); // MTU
  mem_b queuesize = DEFAULT_QUEUE_SIZE * DEFAULT_PACKET_SIZE;

  int algo = UNCOUPLED;
  double epsilon = 1;
  int ssthresh = 15;
  int degree = DEFAULT_DEGREE;
  uint32_t reconf_delay = DEFAULT_RECONF_DELAY;
  string opt_method = "sipring";

  int no_of_nodes = DEFAULT_NODES;

  string flowfile;       // so we can read the flows from a specified file
  double simtime;        // seconds
  double utiltime = .01; // seconds

  // stringstream filename(ios_base::out);
  int i = 1;
  // filename << "logout.dat";

  while (i < argc)
  {
    //   if (!strcmp(argv[i],"-o")){
    //       filename.str(std::string());
    //       filename << argv[i+1];
    //       i++;
    //   } else
    if (!strcmp(argv[i], "-nodes"))
    {
      no_of_nodes = atoi(argv[i + 1]);
      cout << "no_of_nodes " << no_of_nodes << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-speed"))
    {
      SPEED = atoi(argv[i + 1]);
      cout << "speed " << SPEED << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-rtt"))
    {
      RTT = atoi(argv[i + 1]);
      cout << "RTT " << RTT << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-ofile"))
    {
      fct_util_out = std::ofstream(argv[i + 1]);
      cout << "ofile " << argv[i + 1] << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-ssthresh"))
    {
      ssthresh = atoi(argv[i + 1]);
      cout << "ssthresh " << ssthresh << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-q"))
    {
      queuesize = memFromPkt(atoi(argv[i + 1]));
      cout << "queuesize " << queuesize << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-rdelay"))
    {
      reconf_delay = atoi(argv[i + 1]);
      cout << "reconf_delay " << reconf_delay << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-deg"))
    {
      degree = atoi(argv[i + 1]);
      cout << "degree " << degree << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-omethod"))
    {
      opt_method = string(argv[i + 1]);
      cout << "opt_method " << opt_method << endl;
      i++;
    }
    else if (!strcmp(argv[i], "UNCOUPLED"))
      algo = UNCOUPLED;
    else if (!strcmp(argv[i], "COUPLED_INC"))
      algo = COUPLED_INC;
    else if (!strcmp(argv[i], "FULLY_COUPLED"))
      algo = FULLY_COUPLED;
    else if (!strcmp(argv[i], "COUPLED_TCP"))
      algo = COUPLED_TCP;
    else if (!strcmp(argv[i], "COUPLED_SCALABLE_TCP"))
      algo = COUPLED_SCALABLE_TCP;
    else if (!strcmp(argv[i], "COUPLED_EPSILON"))
    {
      algo = COUPLED_EPSILON;
      if (argc > i + 1)
      {
        epsilon = atof(argv[i + 1]);
        i++;
      }
      printf("Using epsilon %f\n", epsilon);
    }
    else if (!strcmp(argv[i], "-flowfile"))
    {
      flowfile = argv[i + 1];
      i++;
    }
    else if (!strcmp(argv[i], "-simtime"))
    {
      simtime = atof(argv[i + 1]);
      i++;
    }
    else if (!strcmp(argv[i], "-utiltime"))
    {
      utiltime = atof(argv[i + 1]);
      i++;
    }
    else
      exit_error(argv[0], argv[i]);
    i++;
  }
  srand(13);

  eventlist.setEndtime(timeFromSec(simtime));
  Clock c(timeFromSec(5 / 100.), eventlist);

  DynFlatScheduler::OptStrategy optstrategy;
  if (opt_method == "sipring")
    optstrategy = DynFlatScheduler::SIPML_RING;
  else if (opt_method == "sipocs")
    optstrategy = DynFlatScheduler::SIPML_OCS;
  else if (opt_method == "dheu")
    optstrategy = DynFlatScheduler::D_HEURISTIC;
  else 
    assert("unknown strategy!" && false);

#if PRINT_PATHS
  filename << ".paths";
  cout << "Logging path choices to " << filename.str() << endl;
  std::ofstream paths(filename.str().c_str());
  if (!paths)
  {
    cout << "Can't open for writing paths file!" << endl;
    exit(1);
  }
#endif

  TcpRtxTimerScanner tcpRtxScanner(timeFromMs(1000), eventlist);
  DemandRecorder demandrecorder = DemandRecorder(degree, &tcpRtxScanner);

  FlatTopology *top = new FlatTopology(no_of_nodes, queuesize, nullptr /* &logfile */, &eventlist, ff, ECN);
  DynFlatScheduler sch = DynFlatScheduler(no_of_nodes, degree, top, optstrategy, &demandrecorder, 10000000ULL * reconf_delay, eventlist);
  // TcpSrc::demand_recorder = &sch.demandrecorder;

      // FFApplication app = FFApplication(top, ssthresh, sinkLogger, traffic_logger, tcpRtxScanner, eventlist);
  FFApplication app = FFApplication(top, ssthresh, &fct_util_out, tcpRtxScanner, eventlist);
  app.load_taskgraph_flatbuf(flowfile);
  app.start_init_tasks();

  // UtilMonitor* UM = new UtilMonitor(top, eventlist);
  // UM->start(timeFromSec(utiltime));

  // Record the setup
  int pktsize = Packet::data_packet_size();
  // logfile.write("# pktsize=" + ntoa(pktsize) + " bytes");
  //logfile.write("# subflows=" + ntoa(subflow_count));
  // logfile.write("# hostnicrate = " + ntoa(SPEED) + " pkt/sec");
  // logfile.write("# corelinkrate = " + ntoa(SPEED*CORE_TO_HOST) + " pkt/sec");
  //logfile.write("# buffer = " + ntoa((double) (queues_na_ni[0][1]->_maxsize) / ((double) pktsize)) + " pkt");
  //double rtt = timeAsSec(timeFromUs(RTT));
  //logfile.write("# rtt =" + ntoa(rtt));

  // GO!
  while (eventlist.doNextEvent())
  {
  }

  fct_util_out << "FinalFinish " << app.final_finish_time << std::endl;
}

string ntoa(double n)
{
  stringstream s;
  s << n;
  return s.str();
}

string itoa(uint64_t n)
{
  stringstream s;
  s << n;
  return s.str();
}
