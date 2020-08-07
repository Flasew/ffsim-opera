#ifndef FF_APP_H
#define FF_APP_H
#include "fat_tree_topology.h"
#undef max
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "eventlist.h"
#include "ndp.h"

/*
 * An application that takes a Flex-flow generated task graph
 * and simulates it on top of the opera network
 */
class FFTask;

class FFApplication {
public:
    FFApplication(FatTreeTopology* top, int cwnd, double pull_rate,  
			NdpRtxTimerScanner & nrts, NdpSinkLoggerSampling & sl, EventList & eventlist, std::string taskgraph);
	~FFApplication();

    void start_init_tasks();

	int cwnd;
	double pull_rate;
    std::unordered_map<int, FFTask*> tasks;
	FatTreeTopology * topology; 
    EventList & eventlist;
	NdpRtxTimerScanner & ndpRtxScanner;
	NdpSinkLoggerSampling & sinkLogger;
};

class FFTask : public EventSource {
public:
    enum FFTaskType {FF_COMM, FF_COMP, FF_INTRA_COMM};

    // FFTask(FFTaskType type, EventList & eventlist,
    //        float rTime, float sTime, float cTime, float xfsz, 
    //        int wid, int gid, int fworker, int tworker, int fGuid, int tGuid);
	FFTask(FFApplication * ffapp, FFTaskType type, EventList & eventlist);

    void add_pretask(FFTask * task);
    void add_nextask(FFTask * task);

    void taskstart();
	void cleanup();
	void start_flow();
    
    virtual void doNextEvent(); // call task event

	FFApplication * ffapp;
	FFTaskType type;
    float readyTime, startTime, computeTime, xferSize;
    int workerId, guid, fromWorker, toWorker, fromGuid, toGuid, fromNode, toNode;
	simtime_picosec sim_start, sim_finish, sim_duration;
	bool started;
	std::unordered_set<FFTask*> preTasks; 
    std::vector<FFTask*> nextTasks;
};

void taskfinish(void * task);

#endif // FF_APP_H
