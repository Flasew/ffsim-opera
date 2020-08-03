#!/bin/sh

../../../src/clos/datacenter/htsim_ndp_fatTree_3to1_k12 -simtime 10.00001 -cwnd 30 -strat perm -nodes 648 -o 25perc.dat -q 46 -pullrate 1 -flowfile ../traffic_gen/flows_25percLoad_10sec_648hosts_3to1.htsim >> FCT_3to1_pfab_cwnd30_25perc.txt &
