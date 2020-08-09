#!/bin/sh

../src/clos/datacenter/htsim_ndp_fc -simtime 15.00001 -cwnd 30 -strat perm -nodes 4 -o 25perc.dat -q 46 -pullrate 1 -flowfile sim_taskgraph_iter10001.json > clos.txt

