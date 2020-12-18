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

/*
 * An application that takes a Flex-flow generated task graph
 * and simulates it on top of the opera network
 */
class FFTask;
class FFDevice;

class FFApplication {
public:
    // FFApplication(Topology* top, int cwnd, double pull_rate,  
	// 		NdpRtxTimerScanner & nrts, NdpSinkLoggerSampling & sl, EventList & eventlist, std::string taskgraph);
    FFApplication(Topology* top, int ss, TcpSinkLoggerSampling & sl, TcpTrafficLogger & tl,
        TcpRtxTimerScanner & rtx, EventList & eventlist, std::string taskgraph);
	~FFApplication();

    void start_init_tasks();

	int cwnd;
	double pull_rate;
    std::unordered_map<uint64_t, FFTask> tasks;
    std::unordered_map<uint64_t, FFDevice> devices;
	Topology * topology; 
    EventList & eventlist;
	// NdpRtxTimerScanner & ndpRtxScanner;
	// NdpSinkLoggerSampling & sinkLogger;
    int ssthresh;
    TcpSinkLoggerSampling & sinkLogger;
    TcpTrafficLogger & tcpTrafficLogger;
    TcpRtxTimerScanner & tcpRtxScanner;
};

class FFDevice {
public:
    enum FFDeviceType {
        DEVICE_GPU,
        DEVICE_CPU,
        DEVICE_GPU_COMM,
        DEVICE_DRAM_COMM,
        DEVICE_NW_COMM,
    };
    int node_id, gpu_id;
    float bandwidth;
    FFDeviceType type;

    int from_gpu, to_gpu, from_node, to_node;

    simtime_picosec busy_up_to;

    FFDevice(std::string type, float bandwidth, int node_id, int gpu_id, 
             int from_node, int to_node, int from_gpu, int to_gpu);
};

class FFTask : public EventSource {
public:
    static FFApplication * ffapp;
    static EventList & evl;

    enum FFTaskType {
        TASK_FORWARD,
        TASK_BACKWARD,
        TASK_COMM,
        TASK_UPDATE,
        TASK_BARRIER,
        TASK_LATENCY,
    };

    void add_nextask(FFTask * task);

    void taskstart();
	void cleanup();
	void start_flow();
    
    virtual void doNextEvent(); // call task event
    void execute_compute();

    FFTaskType type;
    FFDevice* device;
    int counter;
    uint64_t xfersize = 0;
    std::vector<uint64_t> next_tasks;
    int src_node, dst_node;
	simtime_picosec ready_time, run_time;
	simtime_picosec start_time, finish_time;

    FFTask(std::string type, FFDevice * device, uint64_t xfersize, float runtime);
};
FFApplication * FFTask::ffapp = nullptr;

void taskfinish(void * task);

#endif // FF_APP_H
