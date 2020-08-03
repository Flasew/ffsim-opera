#include <fstream>
#include <streambuf>
#include <iostream>
#include <assert.h>

#include "ffapp.h"
#include "json.hpp"
#include "ndp.h"
#include "rlb.h"

using json = nlohmann::json;

FFApplication::FFApplication(DynExpTopology* top, int cwnd, double pull_rate, int rlb_cutoff, 
        NdpRtxTimerScanner & nrts, NdpSinkLoggerSampling & sl, EventList & eventlist, std::string taskgraph) 
    : topology(top), cwnd(cwnd), pull_rate(pull_rate), ndpRtxScanner(nrts), sinkLogger(sl), eventlist(eventlist) {
    
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
        FFTask * task;
        if (task_type == "inter-communication") {            
            task = new FFTask(this, FFTask::FF_COMM, eventlist);
            task->fromGuid = jstask["fromTask"].get<int>();
            task->toGuid = jstask["toTask"].get<int>();
            task->fromWorker = jstask["fromWorker"].get<int>();
            task->toWorker = jstask["toWorker"].get<int>();
            task->fromNode = jstask["fromNode"].get<int>();
            task->toNode = jstask["toNode"].get<int>();
            task->xferSize = jstask["xferSize"].get<float>();
        } else if (task_type == "intra-communication") { 
            task = new FFTask(this, FFTask::FF_INTRA_COMM, eventlist);
            task->fromGuid = jstask["fromTask"].get<int>();
            task->toGuid = jstask["toTask"].get<int>();
            task->fromWorker = jstask["fromWorker"].get<int>();
            task->toWorker = jstask["toWorker"].get<int>();
            task->xferSize = jstask["xferSize"].get<float>();
        } else {
            task = new FFTask(this, FFTask::FF_COMP, eventlist);
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

FFApplication::~FFApplication() {
    for (auto task: tasks) {
        delete task.second;
    }
}

void FFApplication::start_init_tasks() {
    simtime_picosec delta = 0;
    int count = 0;
    for (auto task: tasks) {
        if (task.second->preTasks.size() == 0) {
            task.second->eventlist().sourceIsPending(*(task.second), delta++);
        }
    }
    std::cerr << "added " << count << " init tasks." << std::endl;
}

FFTask::FFTask(FFApplication * app, FFTaskType type, EventList & eventlist)
    : ffapp(app), type(type), EventSource(eventlist, "FFTask") {
    if (type == FF_COMP) {
        fromNode = toNode = fromWorker = toWorker = fromGuid = toGuid = xferSize = -1;
    }
}

// FFTask::FFTask(FFTask::FFTaskType type, EventList & eventlist, //          float rTime, float sTime, float cTime, float xfsz, 
//          int wid, int gid, int fworker, int tworker, int fGuid, int tGuid)
//     : EventSource(eventlist, "FFTask") {

// }

void FFTask::add_pretask(FFTask * task) {
    preTasks.insert(task);
}

void FFTask::add_nextask(FFTask * task) {
    nextTasks.push_back(task);
}

void FFTask::taskstart() {
    if (preTasks.size() != 0 || started) {
        return;
    }
    sim_start = eventlist().now() + 1;
    started = true;

    if (type == FFTask::FF_COMM) {
        start_flow();
    } 
    else {
        sim_duration = (simtime_picosec)(computeTime * 1000000000000ULL); 
        cleanup();
    }
}

void FFTask::cleanup() {
    sim_finish = sim_start + sim_duration;
    for (FFTask * task: nextTasks) {
        task->preTasks.erase(this);
        eventlist().sourceIsPending(*task, sim_finish);
    }
}

void FFTask::doNextEvent() {
    taskstart();
}

void FFTask::start_flow() {
    
    // from ndp main application: generate flow

    if (xferSize < ffapp->rlb_cutoff) { // priority flow, sent it over NDP

        // generate an NDP source/sink:
        NdpSrc* flowSrc = new NdpSrc(ffapp->topology, nullptr, nullptr, eventlist(), fromWorker, toWorker, taskfinish, this);
        flowSrc->setCwnd(ffapp->cwnd * Packet::data_packet_size()); // congestion window
        flowSrc->set_flowsize(xferSize); // bytes

        // Set the pull rate to something reasonable.
        // we can be more aggressive if the load is lower
        NdpPullPacer* flowpacer = new NdpPullPacer(eventlist(), ffapp->pull_rate); // 1 = pull at line rate
        //NdpPullPacer* flowpacer = new NdpPullPacer(eventlist(), .17);

        NdpSink* flowSnk = new NdpSink(ffapp->topology, flowpacer, fromWorker, toWorker);
        ffapp->ndpRtxScanner.registerNdp(*flowSrc);

        // set up the connection event
        flowSrc->connect(*flowSnk, sim_start);

        ffapp->sinkLogger.monitorSink(flowSnk);

    }  else { // background flow, send it over RLB

        // generate an RLB source/sink:

        RlbSrc* flowSrc = new RlbSrc(ffapp->topology, NULL, NULL, eventlist(), fromWorker, toWorker);
        // debug:
        //cout << "setting flow size to " << vtemp[2] << " bytes..." << endl;
        flowSrc->set_flowsize(xferSize); // bytes

        RlbSink* flowSnk = new RlbSink(ffapp->topology, eventlist(), fromWorker, toWorker, taskfinish, this);

        // set up the connection event
        flowSrc->connect(*flowSnk, sim_start);

    }
}

// for comunication task
void taskfinish(void * task) {
    FFTask * fftask = (FFTask*) task;
    assert(fftask->type == FFTask::FF_COMM);

    fftask->sim_finish = fftask->eventlist().now();
    fftask->sim_duration = (fftask->sim_finish - fftask->sim_start);

    fftask->cleanup();
}
