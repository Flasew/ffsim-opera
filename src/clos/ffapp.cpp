#include <fstream>
#include <streambuf>
#include <iostream>
#include <assert.h>

#include "ffapp.h"
#include "ndp.h"
#include "dctcp.h"
#include "route.h"

#include "json.hpp"

using json = nlohmann::json;

FFApplication * FFTask::ffapp;

// FFApplication::FFApplication(Topology* top, int cwnd, double pull_rate,  
// 			NdpRtxTimerScanner & nrts, NdpSinkLoggerSampling & sl, EventList & eventlist, std::string taskgraph)
//     : topology(top), cwnd(cwnd), pull_rate(pull_rate), ndpRtxScanner(nrts), sinkLogger(sl), eventlist(eventlist) {
FFApplication::FFApplication(Topology* top, int ss, TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl,
    TcpRtxTimerScanner & rtx, EventList & eventlist)
    : topology(top), ssthresh(ss), eventlist(eventlist), sinkLogger(sl), tcpTrafficLogger(tl), tcpRtxScanner(rtx){

    FFTask::ffapp = this;
    // FFTask::evl = this->eventlist;
   
}

FFApplication::~FFApplication() {

    for (auto item: tasks) {
        delete item.second;
    }
    for (auto item: devices) {
        delete item.second;
    }

}

void FFApplication::load_taskgraph_json(std::string & taskgraph) {
    // read the taskgraph and parse it
    std::ifstream t(taskgraph);
    std::string tg_str;

    t.seekg(0, std::ios::end);   
    tg_str.reserve(t.tellg());
    t.seekg(0, std::ios::beg);

    tg_str.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());    

    auto tg_json = json::parse(tg_str);

    for (auto & jsdev: tg_json["devices"]) {
        devices[jsdev["deviceid"].get<uint64_t>()] = new FFDevice(
            jsdev["type"].get<std::string>(), 
            jsdev["bandwidth"].get<float>(), 
            jsdev["nodeid"].get<int>(), 
            jsdev["gpuid"].get<int>(), 
            jsdev["fromnode"].get<int>(), 
            jsdev["tonode"].get<int>(), 
            jsdev["fromgpu"].get<int>(), 
            jsdev["togpu"].get<int>()
        );
    }

    unordered_map<uint64_t, unsigned int> counters;

    for (auto & jstask: tg_json["tasks"]) {
        uint64_t this_task = jstask["taskid"].get<uint64_t>();
        tasks[this_task] = new FFTask(
            jstask["type"].get<std::string>(), 
            devices[jstask["deviceid"].get<uint64_t>()], 
            jstask["xfersize"].get<uint64_t>(), 
            jstask["runtime"].get<float>()
        );
              
        for (auto & jsnext: jstask["next_tasks"]) {
            uint64_t next_id = jsnext.get<uint64_t>();
            tasks[this_task]->next_tasks.push_back(next_id);
            if (counters.find(next_id) == counters.end()) {
                counters[next_id] = 1;
            }
            else {
                counters[next_id]++;
            }
        }
    }

    for (auto item: counters) {
        tasks[item.first]->counter = item.second;
    }
}

void FFApplication::load_taskgraph_protobuf(std::string & taskgraph) {
    TaskGraphProtoBuf::TaskGraph tg;
    std::fstream input(taskgraph, std::ios::in | std::ios::binary);
    if (!tg.ParseFromIstream(&input)) {
        std::cerr << "Failed to parse taskgraph." << std::endl;
        assert(false);
    }

    // load device 
    for (int i = 0; i < tg.devices_size(); i++) {
        const TaskGraphProtoBuf::Device & tg_device = tg.devices(i);
        devices[tg_device.deviceid()] = new FFDevice(
            tg_device.type(),
            tg_device.bandwidth(),
            tg_device.nodeid(),
            tg_device.gpuid(),
            tg_device.fromnode(),
            tg_device.tonode(),
            tg_device.fromgpu(),
            tg_device.togpu()
        );
    }

    unordered_map<uint64_t, unsigned int> counters;

    for (int i = 0; i < tg.tasks_size(); i++) {
        TaskGraphProtoBuf::Task this_task = tg.tasks(i);
        tasks[this_task.taskid()] = new FFTask(
            this_task.type(), 
            devices[this_task.deviceid()], 
            this_task.xfersize(), 
            this_task.runtime()
        );
              
        for (int j = 0; j < this_task.nexttasks_size(); j++) {
            uint64_t next_id = this_task.nexttasks(j);
            tasks[this_task.taskid()]->next_tasks.push_back(next_id);
            if (counters.find(next_id) == counters.end()) {
                counters[next_id] = 1;
            }
            else {
                counters[next_id]++;
            }
        }
    }

    for (auto item: counters) {
        tasks[item.first]->counter = item.second;
    }
    
}

void FFApplication::start_init_tasks() {
    simtime_picosec delta = 0;
    int count = 0;
    for (auto task: tasks) {
        //std::cerr << "guid:" << task.second->guid << "size: " << task.second->preTasks.size() << std::endl;
        FFTask * t = task.second;
        if (t->counter == 0) {
            t->state = FFTask::TASK_READY;
            t->eventlist().sourceIsPending(*t, delta++);
            count++;
        }
    }
    std::cerr << "added " << count << " init tasks." << std::endl;
}

FFTask::FFTask(std::string type, FFDevice * device, uint64_t xfersize, 
    float runtime): EventSource(FFTask::ffapp->eventlist, "FFTask") {

    if (type == "TASK_FORWARD") {
        this->type = FFTaskType::TASK_FORWARD;
    }
    else if (type == "TASK_BACKWARD") {
        this->type = FFTaskType::TASK_BACKWARD;
    }
    else if (type == "TASK_COMM") {
        this->type = FFTaskType::TASK_COMM;
    }
    else if (type == "TASK_UPDATE") {
        this->type = FFTaskType::TASK_UPDATE;
    }
    else if (type == "TASK_BARRIER") {
        this->type = FFTaskType::TASK_BARRIER;
    }
    else {
        throw "Unsupported task type!";
    }

    this->state = TASK_NOT_READY;
    this->device = device;
    this->run_time = runtime * 1000000000ULL;
    this->xfersize = xfersize;

    if (device->type == FFDevice::DEVICE_NW_COMM) {
        this->src_node = device->from_node;
        this->dst_node = device->to_node;
    }
    else {
        this->src_node = this->dst_node = -1; 
    }

    ready_time = 0;
    start_time = 0;
    finish_time = 0;
    counter = 0;
}

FFTask::FFTask(TaskGraphProtoBuf::Task_SimTaskType type, FFDevice * device, 
         uint64_t xfersize, float runtime): EventSource(FFTask::ffapp->eventlist, "FFTask") {

    if (type == TaskGraphProtoBuf::Task_SimTaskType_TASK_FORWARD) {
        this->type = FFTaskType::TASK_FORWARD;
    }
    else if (type == TaskGraphProtoBuf::Task_SimTaskType_TASK_BACKWARD) {
        this->type = FFTaskType::TASK_BACKWARD;
    }
    else if (type == TaskGraphProtoBuf::Task_SimTaskType_TASK_COMM) {
        this->type = FFTaskType::TASK_COMM;
    }
    else if (type == TaskGraphProtoBuf::Task_SimTaskType_TASK_UPDATE) {
        this->type = FFTaskType::TASK_UPDATE;
    }
    else if (type == TaskGraphProtoBuf::Task_SimTaskType_TASK_BARRIER) {
        this->type = FFTaskType::TASK_BARRIER;
    }
    else {
        throw "Unsupported task type!";
    }

    this->state = TASK_NOT_READY;
    this->device = device;
    this->run_time = runtime * 1000000000ULL;
    this->xfersize = xfersize;

    if (device->type == FFDevice::DEVICE_NW_COMM) {
        this->src_node = device->from_node;
        this->dst_node = device->to_node;
    }
    else {
        this->src_node = this->dst_node = -1; 
    }

    ready_time = 0;
    start_time = 0;
    finish_time = 0;
    counter = 0;
}

FFTask::FFTask(FFTask::FFTaskType type):
    EventSource(FFTask::ffapp->eventlist, "FFTask") {

    this->type = type;
}

void FFTask::taskstart() {
    // std::cerr << "Guid: " << guid << " try start at " << eventlist().now() << std::endl;
    assert(counter == 0);

    if (type == FFTask::TASK_COMM && device->type == FFDevice::DEVICE_NW_COMM) {
        start_flow();
    } 
    else {
        execute_compute();
    }
}

void FFTask::execute_compute() {

    if (this->state == FFTask::TASK_NOT_READY) {
        std::cerr << "ERROR: Executing not ready task!" << std::endl;
        assert(false);
    }

    if (this->state == FFTask::TASK_FINISHED) {
        std::cerr << "ERROR: Executing finished task!" << std::endl;
        assert(false);
    }

    // check if the device has running task. If not, schedule this task.
    // Otherwise schedule it at the time the other task finishes. 
    if (this->state == FFTask::TASK_READY) {
        if (device->state == FFDevice::DEVICE_IDLE) {
            std::cerr << "Task " << (uint64_t)this << " starts at " << eventlist().now() << std::endl;
            this->state = FFTask::TASK_RUNNING;
            device->state = FFDevice::DEVICE_BUSY;
            start_time = eventlist().now();
            finish_time = start_time + run_time;
            eventlist().sourceIsPending(*this, finish_time);
            device->busy_up_to = finish_time;
        }
        else {
            // std::cerr << "Task " << (uint64_t)this << " dev busy, reschedule at " << device->busy_up_to << std::endl;
            eventlist().sourceIsPending(*this, device->busy_up_to);
        }
    }
    // This means this task has finished
    else if (this->state == FFTask::TASK_RUNNING) {
        std::cerr << "Task " << (uint64_t)this << " finishes at " << eventlist().now() << std::endl;
        assert(device->state == FFDevice::DEVICE_BUSY);

        this->state = FFTask::TASK_FINISHED;
        device->state = FFDevice::DEVICE_IDLE;

        cleanup();
    }
    
}

void FFTask::cleanup() {
    for (uint64_t next_id: next_tasks) {
        FFTask * task = FFTask::ffapp->tasks[next_id];
        task->counter--;
        std::cerr << (uint64_t)this << " -> Task " << (uint64_t)task << " counter at " << task->counter << std::endl;
        if (task->counter == 0) {
            task->ready_time = finish_time;
            task->state = FFTask::TASK_READY;
            eventlist().sourceIsPending(*task, task->ready_time);
        }
    }
}

void FFTask::doNextEvent() {
    taskstart();
}

void FFTask::start_flow() {
    
    std::cerr << "task: " << (uint64_t)this << " start flow (" << src_node << ", " << dst_node << ")\n";
    start_time = ready_time;
    // // from ndp main application: generate flow

    // NdpSrc* flowSrc = new NdpSrc(nullptr, nullptr, eventlist(), src_node, dst_node, taskfinish, (void*)this);
    // flowSrc->setCwnd(ffapp->cwnd*Packet::data_packet_size());
    // flowSrc->set_flowsize(xfersize); // bytes
    // NdpPullPacer* flowpacer = new NdpPullPacer(eventlist(), ffapp->pull_rate); // 1 = pull at line rate   
    // NdpSink* flowSnk = new NdpSink(flowpacer);
    // ffapp->ndpRtxScanner.registerNdp(*flowSrc);
    // Route* routeout, *routein;

    // vector<const Route*>* srcpaths = ffapp->topology->get_paths(src_node, dst_node);
    // routeout = new Route(*(srcpaths->at(0)));
    // routeout->push_back(flowSnk);

    // vector<const Route*>* dstpaths = ffapp->topology->get_paths(dst_node, src_node);
    // routein = new Route(*(dstpaths->at(0)));
    // routein->push_back(flowSrc);

    // flowSrc->connect(*routeout, *routein, *flowSnk, ready_time);

    // flowSrc->set_paths(srcpaths);
    // flowSnk->set_paths(dstpaths);
    // ffapp->sinkLogger.monitorSink(flowSnk);
    
    DCTCPSrc* flowSrc = new DCTCPSrc(NULL, &ffapp->tcpTrafficLogger, eventlist(), src_node, dst_node, taskfinish, this);
    TcpSink* flowSnk = new TcpSink();
    flowSrc->set_flowsize(xfersize); // bytes
    flowSrc->set_ssthresh(ffapp->ssthresh*Packet::data_packet_size());
    flowSrc->_rto = timeFromMs(1);
    
    ffapp->tcpRtxScanner.registerTcp(*flowSrc);

    Route* routeout, *routein;

    int choice = 0;
    vector<const Route*>* srcpaths = ffapp->topology->get_paths(src_node, dst_node);
    choice = rand()%srcpaths->size(); // comment this out if we want to use the first path
    routeout = new Route(*(srcpaths->at(choice)));
    routeout->push_back(flowSnk);

    choice = 0;
    vector<const Route*>* dstpaths = ffapp->topology->get_paths(dst_node, src_node);
    // choice = rand()%dstpaths->size(); // comment this out if we want to use the first path
    routein = new Route(*(dstpaths->at(choice)));
    routein->push_back(flowSrc);

    flowSrc->connect(*routeout, *routein, *flowSnk, start_time);

#ifdef PACKET_SCATTER 
    flowSrc->set_paths(srcpaths);
    flowSnk->set_paths(dstpaths);
#endif
    delete srcpaths;
    delete dstpaths;

}

// for comunication task
void taskfinish(void * task) {

    FFTask * fftask = static_cast<FFTask*>(task);
    std::cerr << (uint64_t)task << " finished, calling back " <<std::endl;

    fftask->finish_time = fftask->eventlist().now();
    fftask->run_time = (fftask->finish_time - fftask->start_time);

    fftask->cleanup();
}

/* FFDevice */
FFDevice::FFDevice(std::string type, float bandwidth, int node_id, int gpu_id,
                   int from_node, int to_node, int from_gpu, int to_gpu) {

    if (type == "DEVICE_GPU") {
        this->type = FFDeviceType::DEVICE_GPU;
    }
    else if (type == "DEVICE_CPU") {
        this->type = FFDeviceType::DEVICE_CPU;
    }
    else if (type == "DEVICE_GPU_COMM") {
        this->type = FFDeviceType::DEVICE_GPU_COMM;
    }
    else if (type == "DEVICE_DRAM_COMM") {
        this->type = FFDeviceType::DEVICE_DRAM_COMM;
    }
    else if (type == "DEVICE_NW_COMM") {
        this->type = FFDeviceType::DEVICE_NW_COMM;
    }
    else {
        throw "Unsupported device type!";
    }

    this->state = FFDevice::DEVICE_IDLE;
    this->bandwidth = bandwidth * 8 * 1000; 
    this->node_id = node_id;
    this->gpu_id = node_id;
    this->from_node = from_node;
    this->to_node = to_node;
    this->from_gpu = from_gpu;
    this->to_gpu = from_gpu;
    
    this->busy_up_to = 0;
}

FFDevice::FFDevice(TaskGraphProtoBuf::Device_DeviceType type, 
                   float bandwidth, int node_id, int gpu_id,
                   int from_node, int to_node, int from_gpu, int to_gpu) {

    if (type == TaskGraphProtoBuf::Device_DeviceType_DEVICE_GPU) {
        this->type = FFDeviceType::DEVICE_GPU;
    }
    else if (type == TaskGraphProtoBuf::Device_DeviceType_DEVICE_CPU) {
        this->type = FFDeviceType::DEVICE_CPU;
    }
    else if (type == TaskGraphProtoBuf::Device_DeviceType_DEVICE_GPU_COMM) {
        this->type = FFDeviceType::DEVICE_GPU_COMM;
    }
    else if (type == TaskGraphProtoBuf::Device_DeviceType_DEVICE_DRAM_COMM) {
        this->type = FFDeviceType::DEVICE_DRAM_COMM;
    }
    else if (type == TaskGraphProtoBuf::Device_DeviceType_DEVICE_NW_COMM) {
        this->type = FFDeviceType::DEVICE_NW_COMM;
    }
    else {
        throw "Unsupported device type!";
    }

    this->state = FFDevice::DEVICE_IDLE;
    this->bandwidth = bandwidth * 8 * 1000; 
    this->node_id = node_id;
    this->gpu_id = node_id;
    this->from_node = from_node;
    this->to_node = to_node;
    this->from_gpu = from_gpu;
    this->to_gpu = from_gpu;
    
    this->busy_up_to = 0;
}

// FFRingAllReduce
FFRingAllreduce::FFRingAllreduce(std::vector<int> ng, uint32_t sz) :
    FFTask(FFTask::TASK_RINGALLREDUCE), node_group(ng), 
    operator_size(sz), finished_partitions(0) { }

void FFRingAllreduce::doNextEvent() {

    // this should only be called once...
    assert(finished_partitions == 0);
    for (size_t i = 0; i < node_group.size(); i++) {
        start_flow(i, 0);
    }

}

void FFRingAllreduce::start_flow(int src_idx, int round) {
    
    assert(round < 2 * ((int)node_group.size() - 1)); // shut the warning up..

    int src_node = node_group[src_idx];
    int dst_node = node_group[(src_idx + 1) % node_group.size()];
    FFRingAllreduceFlow * f = new FFRingAllreduceFlow();
    f->ar = this;
    f->src_idx = src_idx;
    f->round = round;

    DCTCPSrc* flowSrc = new DCTCPSrc(NULL, &ffapp->tcpTrafficLogger, 
        eventlist(), src_node, dst_node, ar_finish, f);
    TcpSink* flowSnk = new TcpSink();
    flowSrc->set_flowsize(operator_size/node_group.size()); // bytes
    flowSrc->set_ssthresh(ffapp->ssthresh*Packet::data_packet_size());
    flowSrc->_rto = timeFromMs(1);
    
    ffapp->tcpRtxScanner.registerTcp(*flowSrc);

    Route* routeout, *routein;

    int choice = 0;
    vector<const Route*>* srcpaths = ffapp->topology->get_paths(src_node, dst_node);
    choice = rand()%srcpaths->size(); // comment this out if we want to use the first path
    routeout = new Route(*(srcpaths->at(choice)));
    routeout->push_back(flowSnk);

    choice = 0;
    vector<const Route*>* dstpaths = ffapp->topology->get_paths(dst_node, src_node);
    // choice = rand()%dstpaths->size(); // comment this out if we want to use the first path
    routein = new Route(*(dstpaths->at(choice)));
    routein->push_back(flowSrc);

    flowSrc->connect(*routeout, *routein, *flowSnk, start_time);

#ifdef PACKET_SCATTER 
    flowSrc->set_paths(srcpaths);
    flowSnk->set_paths(dstpaths);
#endif
    delete srcpaths;
    delete dstpaths;

}

void ar_finish(void * arinfo) {
    
    FFRingAllreduceFlow * f = static_cast<FFRingAllreduceFlow*>(arinfo);
    FFRingAllreduce * ar = f->ar;

    if ((f->round + 1) == (2 * ((int)ar->node_group.size() - 1))) {
        ar->finished_partitions++;
        if (ar->finished_partitions == (int)ar->node_group.size()) {
            ar->finish_time = ar->eventlist().now();
        } 
    }
    else {
        f->ar->start_flow((f->src_idx + 1) % ar->node_group.size(), f->round + 1);
    }
    delete f;

}