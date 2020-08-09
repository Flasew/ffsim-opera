#!/bin/sh

../src/clos/datacenter/htsim_ndp_fatTree -simtime 15.00001 -cwnd 1000 -strat perm -nodes 16 -o 25perc.dat -q 1000 -pullrate 1 -flowfile sim_taskgraph_iter10001.json > clos.txt
