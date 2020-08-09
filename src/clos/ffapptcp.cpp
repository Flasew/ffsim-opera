#include <fstream>
#include <streambuf>
#include <iostream>
#include <assert.h>

#include "ffapptcp.h"
#include "tcp.h"
#include "route.h"

#include "json.hpp"

using json = nlohmann::json;

FFApplicationTCP::FFApplicationTCP(Topology* top, int ss, TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl,
    TcpRtxTimerScanner & rtx, EventList & eventlist, std::string taskgraph)
    : topology(top), ssthresh(ss), eventlist(eventlist), sinkLogger(sl), tcpTrafficLogger(tl), tcpRtxScanner(rtx){

    // read the taskgraph and parse it
    std::ifstream t(taskgraph);
    std::string tg_str;

    t.seekg(0, std::ios::end);   
    tg_str.reserve(t.tellg());
    t.seekg(0, std::ios::beg);

    tg_str.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());    

    auto tg_json = json::parse(tg_str);

    for (auto & jstask: tg_json["tasks"]) {
        string task_type = jstask["type"].get<std::string>();
        FFTaskTCP * task;
        if (task_type == "inter-communication") {            
            task = new FFTaskTCP(this, FFTaskTCP::FF_COMM, eventlist);
            task->fromGuid = jstask["fromTask"].get<int>();
            task->toGuid = jstask["toTask"].get<int>();
            task->fromWorker = jstask["fromWorker"].get<int>();
            task->toWorker = jstask["toWorker"].get<int>();
            task->fromNode = jstask["fromNode"].get<int>();
            task->toNode = jstask["toNode"].get<int>();
            task->xferSize = jstask["xferSize"].get<float>();
        } else if (task_type == "intra-communication") { 
            task = new FFTaskTCP(this, FFTaskTCP::FF_INTRA_COMM, eventlist);
            task->fromGuid = jstask["fromTask"].get<int>();
            task->toGuid = jstask["toTask"].get<int>();
            task->fromWorker = jstask["fromWorker"].get<int>();
            task->toWorker = jstask["toWorker"].get<int>();
            task->xferSize = jstask["xferSize"].get<float>();
        } else {
            task = new FFTaskTCP(this, FFTaskTCP::FF_COMP, eventlist);
        }
        task->guid = jstask["guid"].get<int>();
        task->workerId = jstask["workerId"].get<int>();
        task->readyTime = jstask["readyTime"].get<float>();
        task->startTime = jstask["startTime"].get<float>();
        task->computeTime = jstask["computeTime"].get<float>();
        
        tasks[task->guid] = task;
    }

    for (auto & jsedge: tg_json["edges"]) {
        int from = jsedge[0].get<int>();
        int to = jsedge[1].get<int>();
        tasks[from]->add_nextask(tasks[to]);
        tasks[to]->add_pretask(tasks[from]);
    }
}

FFApplicationTCP::~FFApplicationTCP() {
    for (auto task: tasks) {
        delete task.second;
    }
}

void FFApplicationTCP::start_init_tasks() {
    simtime_picosec delta = 0;
    int count = 0;
    for (auto task: tasks) {
        //std::cerr << "guid:" << task.second->guid << "size: " << task.second->preTasks.size() << std::endl;
        if (task.second->preTasks.size() == 0) {
            task.second->eventlist().sourceIsPending(*(task.second), delta++);
            count++;
        }
    }
    std::cerr << "added " << count << " init tasks." << std::endl;
}

FFTaskTCP::FFTaskTCP(FFApplicationTCP * app, FFTaskTypeTCP type, EventList & eventlist)
: ffapp(app), type(type), EventSource(eventlist, "FFTaskTCP") {
    if (type == FF_COMP) {
        fromNode = toNode = fromWorker = toWorker = fromGuid = toGuid = xferSize = -1;
    }
    started = false;
}

// FFTaskTCP::FFTaskTCP(FFTaskTCP::FFTaskTypeTCP type, EventList & eventlist, //          float rTime, float sTime, float cTime, float xfsz, 
//          int wid, int gid, int fworker, int tworker, int fGuid, int tGuid)
//     : EventSource(eventlist, "FFTaskTCP") {

// }

void FFTaskTCP::add_pretask(FFTaskTCP * task) {
    preTasks.insert(task);
}

void FFTaskTCP::add_nextask(FFTaskTCP * task) {
    nextTasks.push_back(task);
}

void FFTaskTCP::taskstart() {
    std::cerr << "Guid: " << guid << " try start at " << eventlist().now() << std::endl;
    if (preTasks.size() != 0 || started) {
        std::cerr << "can't start, pre.size = " << preTasks.size() << std::endl;
        return;
    }
    sim_start = eventlist().now() + 1;
    started = true;
    std::cerr << "started" << std::endl;

    if (type == FFTaskTCP::FF_COMM) {
        start_flow();
    } 
    else {
        sim_duration = (simtime_picosec)((double)computeTime * 1000000000ULL); 
        cleanup();
    }
}

void FFTaskTCP::cleanup() {
    sim_finish = sim_start + sim_duration;
    std::cerr << "Finish " << guid << " at " << sim_finish << std::endl;
    for (FFTaskTCP * task: nextTasks) {
        task->preTasks.erase(this);
        std::cerr << "Finish " << guid << ", Guid: " << task->guid << " has " << task->preTasks.size() << "pres." << std::endl;
        eventlist().sourceIsPending(*task, sim_finish);
    }
}

void FFTaskTCP::doNextEvent() {
    taskstart();
}

void FFTaskTCP::start_flow() {

    std::cerr << "Guid: " << guid << " start flow (" << fromNode << ", " << toNode << ")\n";
    // from ndp main application: generate flow

    TcpSrc* flowSrc = new TcpSrc(NULL, &ffapp->tcpTrafficLogger, eventlist(), fromNode, toNode, fftcptaskfinish, this);
    TcpSink* flowSnk = new TcpSink();
    flowSrc->set_flowsize(xferSize); // bytes
    flowSrc->set_ssthresh(ffapp->ssthresh*Packet::data_packet_size());
    flowSrc->_rto = timeFromMs(1);
    
    ffapp->tcpRtxScanner.registerTcp(*flowSrc);

    Route* routeout, *routein;

    int choice = 0;
    vector<const Route*>* srcpaths = ffapp->topology->get_paths(fromNode, toNode);
    choice = rand()%srcpaths->size(); // comment this out if we want to use the first path
    routeout = new Route(*(srcpaths->at(choice)));
    routeout->push_back(flowSnk);

    choice = 0;
    vector<const Route*>* dstpaths = ffapp->topology->get_paths(toNode, fromNode);
    choice = rand()%dstpaths->size(); // comment this out if we want to use the first path
    routein = new Route(*(dstpaths->at(choice)));
    routein->push_back(flowSrc);

    flowSrc->connect(*routeout, *routein, *flowSnk, timeFromNs(sim_start));

#ifdef PACKET_SCATTER
    flowSrc->set_paths(srcpaths);
    flowSnk->set_paths(dstpaths);
#endif
    
    ffapp->sinkLogger.monitorSink(flowSnk);

}

// for comunication task
void fftcptaskfinish(void * task) {

    FFTaskTCP * fftaskTCP = (FFTaskTCP*) task;
    std::cerr << "Guid: " << fftaskTCP->guid << " finished, calling back " <<std::endl;
    assert(fftaskTCP->type == FFTaskTCP::FF_COMM);

    fftaskTCP->sim_finish = fftaskTCP->eventlist().now();
    fftaskTCP->sim_duration = (fftaskTCP->sim_finish - fftaskTCP->sim_start);

    fftaskTCP->cleanup();
}
