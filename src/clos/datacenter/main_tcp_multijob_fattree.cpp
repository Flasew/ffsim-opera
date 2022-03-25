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
//#include "connection_matrix.h"

// Choose the topology here:
// #include "test_topology.h"
#include "fat_tree_topology.h"
#include "ffapp.h"

#include <list>

// Simulation params

#define PRINT_PATHS 0

#define PERIODIC 0
#include "main.h"

uint32_t RTT_rack = 500; // ns
uint32_t RTT_net = 500;  // ns
uint32_t SPEED;
std::ofstream fct_util_out;

int DEFAULT_NODES = 128;

FirstFit *ff = NULL;
//unsigned int subflow_count = 8; // probably not necessary ???

#define DEFAULT_PACKET_SIZE 9000 // full packet (including header), Bytes
#define DEFAULT_HEADER_SIZE 64   // header size, Bytes
#define DEFAULT_QUEUE_SIZE 200

#define DEFAULT_SPEED 40000

string ntoa(double n);
string itoa(uint64_t n);

EventList eventlist;
Logfile *lg;

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

  int no_of_nodes = DEFAULT_NODES;
  SPEED = DEFAULT_SPEED;
  // fct_util_out = std::cout;

  string flowfiles;       // so we can read the flows from a specified file
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
    else if (!strcmp(argv[i], "-rttrack"))
    {
      RTT_rack = atoi(argv[i + 1]);
      cout << "RTT_rack " << RTT_rack << endl;
      i++;
    }
    else if (!strcmp(argv[i], "-rttnet"))
    {
      RTT_net = atoi(argv[i + 1]);
      cout << "rttnet " << RTT_net << endl;
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
    else if (!strcmp(argv[i], "-flowfiles"))
    {
      flowfiles = argv[i + 1];
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

  // parse flow files
  std::vector<string> flowfile_arr;
  istringstream ss(flowfiles);
  while (ss) {
    string next;
    if (!getline(ss, next, ',')) break;
    flowfile_arr.push_back(next);
  }

  // parse the number of nodes needed by each job.
  // this assumes the file is in the form "model_nnode.fbuf"
  std::vector<int> nnodes_for_flowfile;
  for (const string & s:  flowfile_arr) {
    size_t start = s.find('_') + 1;
    size_t end = s.find('.');
    nnodes_for_flowfile.push_back(std::stoi(s.substr(start, end-start)));
  }

  //cout <<  "Using algo="<<algo<< " epsilon=" << epsilon << endl;

  //Logfile logfile(filename.str(), eventlist);

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

  //lg = &logfile;

  // !!!!!!!!!!!!!!!!!!!!!!!
  //logfile.setStartTime(timeFromSec(10));

  // TcpSinkLoggerSampling sinkLogger = TcpSinkLoggerSampling(timeFromUs(50.), eventlist);
  //logfile.addLogger(sinkLogger);
  // TcpTrafficLogger traffic_logger = TcpTrafficLogger();
  // traffic_logger.fct_util_out = &fct_util_out;
  //logfile.addLogger(traffic_logger);

  TcpRtxTimerScanner tcpRtxScanner(timeFromMs(1), eventlist);

  FatTreeTopology *top = new FatTreeTopology(no_of_nodes, queuesize, nullptr /*&logfile*/, &eventlist, ff, ECN);
  // note that 'queuesize' does not pass throuf_nodesgh currently for RANDOM...

  std::unordered_set<int> candidates;
  for (int i = 0; i < no_of_nodes; i++) candidates.insert(i);

  std::vector<FFApplication*> ffapps;
  for (int i = 0; i < flowfile_arr.size(); i++) {
    std::vector<int> nodes = FFApplication::choose_gpus(candidates, nnodes_for_flowfile[i]);
    std::cerr << flowfile_arr[i] << ": ";
    for (int n: nodes) std::cerr << n << ", ";
    std::cerr << endl;
    FFApplication * app = new FFApplication(top, ssthresh, &fct_util_out, nodes, tcpRtxScanner, eventlist);
    app->load_taskgraph_flatbuf(flowfile_arr[i]);
    app->start_init_tasks();
    ffapps.push_back(app);
  }
  

  // UtilMonitor* UM = new UtilMonitor(top, eventlist);
  // UM->start(timeFromSec(utiltime));

  // Record the setup
  int pktsize = Packet::data_packet_size();
  //logfile.write("# pktsize=" + ntoa(pktsize) + " bytes");
  //logfile.write("# subflows=" + ntoa(subflow_count));
  //logfile.write("# hostnicrate = " + ntoa(SPEED) + " pkt/sec");
  //logfile.write("# corelinkrate = " + ntoa(SPEED*CORE_TO_HOST) + " pkt/sec");
  //logfile.write("# buffer = " + ntoa((double) (queues_na_ni[0][1]->_maxsize) / ((double) pktsize)) + " pkt");
  //double rtt = timeAsSec(timeFromUs(RTT));
  //logfile.write("# rtt =" + ntoa(rtt));

  // GO!
  while (eventlist.doNextEvent())
  {
  }

  for (int i = 0; i < flowfile_arr.size(); i++) {
    fct_util_out << "FinalFinish_" << flowfile_arr[i] << " " << ffapps[i]->first_iter_time << std::endl;
  }
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
