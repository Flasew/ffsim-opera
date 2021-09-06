#ifndef FF_APP_H
#define FF_APP_H

#include "loggers.h"
// #undef max 

#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "topology.h"
#include "flat_topology.h"
#include "eventlist.h"
#include "ndp.h"

#include "taskgraph_generated.h"
// #include "taskgraph.pb.h"

/*
 * An application that takes a Flex-flow generated task graph
 * and simulates it on top of the opera network
 */

class FFApplication;

class FFDevice {
public:
    enum FFDeviceType {
        DEVICE_GPU,
        DEVICE_CPU,
        DEVICE_GPU_COMM,
        DEVICE_DRAM_COMM,
        DEVICE_NW_COMM,
    };

    enum FFDeviceState {
        DEVICE_IDLE,
        DEVICE_BUSY,
    };
    FFApplication * ffapp;

    int node_id, gpu_id;
    float bandwidth;
    FFDeviceType type;
    FFDeviceState state;

    int from_gpu, to_gpu, from_node, to_node;

    simtime_picosec busy_up_to;
    // int nqueued_tasks;

    FFDevice(FFApplication * ffapp, std::string type, float bandwidth, int node_id, int gpu_id, 
             int from_node, int to_node, int from_gpu, int to_gpu);
    FFDevice(FFApplication * ffapp, FlatBufTaskGraph::DeviceType devtype, uint64_t nodeid, 
             uint64_t deviceproperty, uint64_t bandwidth);
};

class FFTask : public EventSource {
public:
    FFApplication * ffapp;
    // static EventList & evl;

    enum FFTaskType {
        TASK_FORWARD,
        TASK_BACKWARD,
        TASK_COMM,
        TASK_UPDATE,
        TASK_BARRIER,
        TASK_LATENCY,
        TASK_ALLREDUCE,
    };

    enum FFTaskState {
        TASK_NOT_READY,
        TASK_READY,
        TASK_RUNNING,
        TASK_FINISHED,
    };

    void add_nextask(FFTask * task);

    void taskstart();
	void cleanup();
	void start_flow();
    
    virtual void doNextEvent(); // call task event
    void execute_compute();

    virtual void reset() {
        state = FFTask::TASK_NOT_READY;
        ready_time = eventlist().now();
        start_time = 0;
        finish_time = 0;
    }

    FFTaskType type;
    FFTaskState state;
    FFDevice* device;
    int counter;
    uint64_t xfersize = 0;
    std::vector<uint64_t> next_tasks;
    int src_node, dst_node;
	simtime_picosec ready_time, run_time;
	simtime_picosec start_time, finish_time;

    FFTask(FFApplication * ffapp, std::string type, FFDevice * device, uint64_t xfersize, float runtime);
    // FFTask(TaskGraphProtoBuf::Task_SimTaskType tasktype, FFDevice * device, uint64_t xfersize, float runtime);
    FFTask(FFApplication * ffapp, FlatBufTaskGraph::SimTaskType tasktype, FFDevice * device, uint64_t xfersize, float runtime);
    FFTask(FFApplication * ffapp, FFTaskType tasktype);
};

class FFNewRingAllreduce;

struct FFNewRingAllreduceFlow {
    FFNewRingAllreduce * ar;
    int id;
    int src_idx; 
    int ring_idx;
    int round;
};

class FFNewRingAllreduce : public FFTask {

public:
    FFNewRingAllreduce(FFApplication * ffapp, std::vector<uint64_t> ng, 
        const std::vector<std::vector<int>>& jumps, uint64_t sz,  double local_runtime = 0);
    ~FFNewRingAllreduce() = default;

    std::vector<uint64_t> node_group; // group of nodes in the order of the ring
    const std::vector<std::vector<int>>& jumps;

    uint64_t operator_size;      // total data size of the operator
    std::vector<int> total_jump;
    int finished_rings;

    std::vector<int> finished_curr_round;
    std::vector<int> curr_round;
    std::vector<std::vector<int>> finished_rounds;

    virtual void doNextEvent();
    virtual void reset() {
        FFTask::reset();
        std::fill(finished_curr_round.begin(), finished_curr_round.end(), 0);
        std::fill(curr_round.begin(), curr_round.end(), 0);
        for (auto & v: finished_rounds) {
            std::fill(v.begin(), v.end(), 0);
        }
        finished_rings = 0;
    }

    void start_flow(int src_idx, const std::vector<int>& jump, int ring_id, int id);
};

void ar_finish_newring(void * arinfo);

class FFRingAllreduce;

struct FFRingAllreduceFlow {
    FFRingAllreduce * ar;
    int id;
    int src_idx;
    int round;
};

class FFRingAllreduce : public FFTask {

public:
    FFRingAllreduce(FFApplication * ffapp, std::vector<uint64_t> ng, uint64_t sz, double local_runtime = 0);
    ~FFRingAllreduce() = default;

    std::vector<uint64_t> node_group; // group of nodes in the order of the ring
    uint32_t operator_size;      // total data size of the operator
    int finished_partitions;     // number of finished partitions

    int finished_curr_round;
    int curr_round;
    std::vector<int> finished_rounds;

    virtual void doNextEvent();
    virtual void reset() {
        FFTask::reset();
        finished_curr_round = 0;
        curr_round = 0;
        finished_partitions = 0;
        std::fill(finished_rounds.begin(), finished_rounds.end(), 0);
    }
    // void start();
    // void start_flow(int src_idx, int round);
    void start_flow(int src_idx, int id);
};

void ar_finish_ring(void * arinfo);


class FFPSAllreduce;

struct FFPSAllreduceFlow {
    FFPSAllreduce * ar;
    int node_idx;
    int direction;
};

class FFPSAllreduce : public FFTask {

public:
    FFPSAllreduce(FFApplication * ffapp, std::vector<uint64_t> ng, uint64_t sz,  double local_runtime = 0);
    ~FFPSAllreduce() = default;

    std::vector<uint64_t> node_group; // group of nodes
    uint32_t operator_size;      // total data size of the operator
    int pserver;

    int curr_round;              // will be 2 (scatter, gather)
    std::vector<int> finished_rounds;
    int finished_curr_round;

    virtual void doNextEvent();
    virtual void reset() {
        FFTask::reset();
        finished_curr_round = 0;
        curr_round = 0;
        std::fill(finished_rounds.begin(), finished_rounds.end(), 0);
    }
    void start_flow(int node_idx, int direction);
};

void ar_finish_ps(void * arinfo);


class FFDPSAllreduce;

// struct FFDPSAllreduceFlow {
//     FFDPSAllreduce * ar;
//     int id;
//     int src_idx;
//     int round;
// };

class FFDPSAllreduce : public FFTask {

public:
    FFDPSAllreduce(FFApplication * ffapp, std::vector<uint64_t> ng, uint64_t sz, double local_runtime = 0);
    ~FFDPSAllreduce() = default;

    std::vector<uint64_t> node_group; // group of nodes in the order of the ring
    uint32_t operator_size;      // total data size of the operator
    int finished_partitions;     // number of finished partitions

    int finished_curr_round;
    int curr_round;

    virtual void doNextEvent();
    virtual void reset() {
        FFTask::reset();
        finished_curr_round = 0;
        curr_round = 0;
        finished_partitions = 0;
    }
    // void start();
    // void start_flow(int src_idx, int round);
    void start_flow(int src_node, int dst_node);
};

void ar_finish_dps(void * ar_ptr);

class FFApplication {
public:

    enum FFAllReduceStrategy {
        FF_RING_AR,
        FF_PS_AR,
        FF_DPS_AR,
        FF_DEFAULT_AR
    };

    // FFApplication(Topology* top, int cwnd, double pull_rate,  
	// 		NdpRtxTimerScanner & nrts, NdpSinkLoggerSampling & sl, EventList & eventlist, std::string taskgraph);
    // FFApplication(Topology* top, int ss, TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl,
    //     TcpRtxTimerScanner & rtx, EventList & eventlist, std::string taskgraph);
    FFApplication(Topology* top, int ss, ofstream * _fstream_out, // TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl, 
        TcpRtxTimerScanner & rtx, EventList & eventlist, FFAllReduceStrategy ars = FFApplication::FF_DEFAULT_AR);
    FFApplication(Topology* top, int ss, ofstream * _fstream_out, std::vector<int> gpus, // TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl, 
        TcpRtxTimerScanner & rtx, EventList & eventlist, FFAllReduceStrategy ars = FFApplication::FF_DEFAULT_AR);
        
	~FFApplication();

    void load_taskgraph_json(std::string & taskgraph);
    // void load_taskgraph_protobuf(std::string & taskgraph);
    void load_taskgraph_flatbuf(std::string & taskgraph);
    void start_init_tasks();

    void reset_and_restart();

    static std::vector<int> choose_gpus(std::unordered_set<int> & candidates, int n);

    static bool LoadFileRaw(const char *name, std::string *buf) {
        std::ifstream ifs(name, std::ifstream::binary);
        if (!ifs.is_open()) {
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

    size_t nnodes, ngpupernode, nswitches;
    
	int cwnd;
	double pull_rate;
    std::unordered_map<uint64_t, FFTask*> tasks;
    std::unordered_map<uint64_t, FFDevice*> devices;
	Topology * topology; 
    int ssthresh;
    EventList & eventlist;
    unordered_map<uint64_t, unsigned int> counters;
	// NdpRtxTimerScanner & ndpRtxScanner;
	// NdpSinkLoggerSampling & sinkLogger;
    // TcpSinkLoggerSampling & sinkLogger;
    // TcpTrafficLogger & tcpTrafficLogger;
    FFAllReduceStrategy allreduce_strategy;
    ofstream * fstream_out;
    TcpRtxTimerScanner & tcpRtxScanner;
    std::unordered_map<uint64_t, std::vector<std::vector<int>>> selected_jumps;
    bool fancy_ring;
    bool finished_once;

    static int total_apps;
    static int finished_apps;

    std::vector<int> gpus;
    simtime_picosec final_finish_time;
    simtime_picosec first_iter_time;
    size_t n_finished_tasks;
};


void taskfinish(void * task);

#endif // FF_APP_H
