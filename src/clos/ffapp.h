#ifndef FF_APP_H
#define FF_APP_H

#include "loggers.h"
#undef max 

#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "topology.h"
#include "eventlist.h"
#include "ndp.h"

#include "taskgraph.pb.h"

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


    int node_id, gpu_id;
    float bandwidth;
    FFDeviceType type;
    FFDeviceState state;

    int from_gpu, to_gpu, from_node, to_node;

    simtime_picosec busy_up_to;
    // int nqueued_tasks;

    FFDevice(std::string type, float bandwidth, int node_id, int gpu_id, 
             int from_node, int to_node, int from_gpu, int to_gpu);
    FFDevice(TaskGraphProtoBuf::Device_DeviceType devtype, float bandwidth, int node_id, int gpu_id, 
             int from_node, int to_node, int from_gpu, int to_gpu);
};

class FFTask : public EventSource {
public:
    static FFApplication * ffapp;
    // static EventList & evl;

    enum FFTaskType {
        TASK_FORWARD,
        TASK_BACKWARD,
        TASK_COMM,
        TASK_UPDATE,
        TASK_BARRIER,
        TASK_LATENCY,
        TASK_RINGALLREDUCE,
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

    FFTaskType type;
    FFTaskState state;
    FFDevice* device;
    int counter;
    uint64_t xfersize = 0;
    std::vector<uint64_t> next_tasks;
    int src_node, dst_node;
	simtime_picosec ready_time, run_time;
	simtime_picosec start_time, finish_time;

    FFTask(std::string type, FFDevice * device, uint64_t xfersize, float runtime);
    FFTask(TaskGraphProtoBuf::Task_SimTaskType tasktype, FFDevice * device, uint64_t xfersize, float runtime);
    FFTask(FFTaskType tasktype);
};

class FFRingAllreduce;

struct FFRingAllreduceFlow {
    FFRingAllreduce * ar;
    int id;
    // int src_idx;
    // int round;
};

class FFRingAllreduce : public FFTask {

public:
    FFRingAllreduce(std::vector<int> ng, uint64_t sz);
    ~FFRingAllreduce() = default;

    std::vector<int> node_group; // group of nodes in the order of the ring
    uint32_t operator_size;      // total data size of the operator
    // int finished_partitions;     // number of finished partitions

    int finished_curr_round;
    int curr_round;
    std::vector<int> finished_rounds;

    virtual void doNextEvent();

    // void start();
    // void start_flow(int src_idx, int round);
    void start_flow(int src_idx, int id);
};

void ar_finish(void * arinfo);

class FFApplication {
public:
    // FFApplication(Topology* top, int cwnd, double pull_rate,  
	// 		NdpRtxTimerScanner & nrts, NdpSinkLoggerSampling & sl, EventList & eventlist, std::string taskgraph);
    // FFApplication(Topology* top, int ss, TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl,
    //     TcpRtxTimerScanner & rtx, EventList & eventlist, std::string taskgraph);
    FFApplication(Topology* top, int ss, TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl,
        TcpRtxTimerScanner & rtx, EventList & eventlist);
        
	~FFApplication();

    void load_taskgraph_json(std::string & taskgraph);
    void load_taskgraph_protobuf(std::string & taskgraph);
    void start_init_tasks();

	int cwnd;
	double pull_rate;
    std::unordered_map<uint64_t, FFTask*> tasks;
    std::unordered_map<uint64_t, FFDevice*> devices;
	Topology * topology; 
    int ssthresh;
    EventList & eventlist;
	// NdpRtxTimerScanner & ndpRtxScanner;
	// NdpSinkLoggerSampling & sinkLogger;
    TcpSinkLoggerSampling & sinkLogger;
    TcpTrafficLogger & tcpTrafficLogger;
    TcpRtxTimerScanner & tcpRtxScanner;

    simtime_picosec final_finish_time;
    size_t n_finished_tasks;
};


void taskfinish(void * task);

#endif // FF_APP_H
