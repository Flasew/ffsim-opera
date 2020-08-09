#!/bin/sh

../src/clos/datacenter/htsim_tcp_fc -simtime 15.00001 -nodes 4 -o 25perc.dat -q 100 -flowfile sim_taskgraph_iter10001.json > clos.txt

