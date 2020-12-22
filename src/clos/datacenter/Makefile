CC=g++
CXX=g++
CFLAGS=-Wall -g -O0
FF_HOME?=/home/weiyangw/FlexFlow
CRT=`pwd`
INCLUDE= -I/$(CRT)/.. -I$(CRT) -I$(FF_HOME)/protobuf/src
#-I$(CRT)/ksp -I$(CRT)/ksp/boost
LIB=-L.. -L$(FF_HOME)/protobuf/src/.libs -lprotobuf
#-Lksp
OBJS=../eventlist.o ../tcppacket.o ../pipe.o ../queue.o ../queue_lossless.o ../queue_lossless_input.o ../queue_lossless_output.o ../ecnqueue.o ../tcp.o ../dctcp.o ../mtcp.o ../loggers.o ../logfile.o ../clock.o ../config.o ../network.o ../qcn.o ../exoqueue.o ../randomqueue.o ../cbr.o ../cbrpacket.o ../sent_packets.o ../ndp.o ../ndppacket.o ../eth_pause_packet.o ../compositequeue.o ../prioqueue.o ../cpqueue.o ../compositeprioqueue.o ../switch.o ../fairpullqueue.o ../route.o ../ffapp.o ../taskgraph.pb.o

all:	htsim_ndp_fatTree_3to1_k12 htsim_ndp_fatTree_6to1_hybrid htsim_ndp_fatTree htsim_ndp_fc htsim_tcp_fc
# htsim_dctcp_fatTree: main_dctcp_fatTree.o firstfit.o ../libhtsim.a fat_tree_topology.o
# 	$(CC) $(CFLAGS) firstfit.o main_dctcp_fatTree.o fat_tree_topology.o $(LIB) -lhtsim -o htsim_dctcp_fatTree

# htsim_tcp_fatTree: main_tcp_fatTree.o firstfit.o ../libhtsim.a fat_tree_topology.o
# 	$(CC) $(CFLAGS) firstfit.o main_tcp_fatTree.o fat_tree_topology.o $(LIB) -lhtsim -o htsim_tcp_fatTree

htsim_ndp_fatTree: main_ndp_fatTree.o firstfit.o ../libhtsim.a fat_tree_topology.o
	$(CC) $(CFLAGS) $(INCLUDE) firstfit.o main_ndp_fatTree.o fat_tree_topology.o $(LIB) -lhtsim -o htsim_ndp_fatTree

htsim_ndp_fc: main_ndp_fc.o firstfit.o ../libhtsim.a fc_topology.o
	$(CC) $(CFLAGS) $(INCLUDE) firstfit.o main_ndp_fc.o ../taskgraph.pb.o fc_topology.o $(LIB) -lhtsim -o htsim_ndp_fc

htsim_tcp_fc: main_tcp_fc.o firstfit.o ../libhtsim.a test_topology.o
	$(CC) $(CFLAGS) $(INCLUDE) firstfit.o main_tcp_fc.o test_topology.o $(OBJS) $(LIB) -lhtsim -o htsim_tcp_fc

htsim_ndp_fatTree_3to1_k12: main_ndp_fatTree_3to1_k12.o firstfit.o ../libhtsim.a fat_tree_topology_3to1_k12.o
	$(CC) $(CFLAGS) firstfit.o main_ndp_fatTree_3to1_k12.o fat_tree_topology_3to1_k12.o $(LIB) -lhtsim -o htsim_ndp_fatTree_3to1_k12

# htsim_ndp_fatTree_3to1_k24: main_ndp_fatTree_3to1_k24.o firstfit.o ../libhtsim.a fat_tree_topology_3to1_k24.o
# 	$(CC) $(CFLAGS) firstfit.o main_ndp_fatTree_3to1_k24.o fat_tree_topology_3to1_k24.o $(LIB) -lhtsim -o htsim_ndp_fatTree_3to1_k24

htsim_ndp_fatTree_6to1_hybrid: main_ndp_fatTree_6to1_hybrid.o firstfit.o ../libhtsim.a fat_tree_topology_6to1_hybrid.o
	$(CC) $(CFLAGS) firstfit.o main_ndp_fatTree_6to1_hybrid.o fat_tree_topology_6to1_hybrid.o $(LIB) -lhtsim -o htsim_ndp_fatTree_6to1_hybrid

# htsim_ndp_fatTree: htsim_ndp_fatTree.o firstfit.o ffapp.o ../libhtsim.a htsim_ndp_fatTree.o
# 	$(CC) $(CFLAGS) firstfit.o htsim_ndp_fatTree.o htsim_ndp_fatTree.o $(LIB) -lhtsim -o htsim_ndp_fatTree

# main_dctcp_fatTree.o: main_dctcp_fatTree.cpp
# 	$(CC) $(INCLUDE) $(CFLAGS) -c main_dctcp_fatTree.cpp

main_tcp_fatTree.o: main_tcp_fatTree.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_tcp_fatTree.cpp

main_ndp_fc.o: main_ndp_fc.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_ndp_fc.cpp

main_tcp_fc.o: main_tcp_fc.cpp 
	$(CC) $(INCLUDE) $(CFLAGS) -c  main_tcp_fc.cpp


main_ndp_fatTree.o: main_ndp_fatTree.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_ndp_fatTree.cpp

main_ndp_fatTree_3to1_k12.o: main_ndp_fatTree_3to1_k12.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_ndp_fatTree_3to1_k12.cpp 

# main_ndp_fatTree_3to1_k24.o: main_ndp_fatTree_3to1_k24.cpp
# 	$(CC) $(INCLUDE) $(CFLAGS) -c main_ndp_fatTree_3to1_k24.cpp

main_ndp_fatTree_6to1_hybrid.o: main_ndp_fatTree_6to1_hybrid.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_ndp_fatTree_6to1_hybrid.cpp

firstfit.o: firstfit.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c firstfit.cpp

test_topology.o: test_topology.cpp test_topology.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c test_topology.cpp

fc_topology.o: fc_topology.cpp fc_topology.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c fc_topology.cpp

fat_tree_topology.o: fat_tree_topology.cpp fat_tree_topology.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c fat_tree_topology.cpp

fat_tree_topology_3to1_k12.o: fat_tree_topology_3to1_k12.cpp fat_tree_topology_3to1_k12.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c fat_tree_topology_3to1_k12.cpp

# fat_tree_topology_3to1_k24.o: fat_tree_topology_3to1_k24.cpp fat_tree_topology_3to1_k24.h topology.h
# 	$(CC) $(INCLUDE) $(CFLAGS) -c fat_tree_topology_3to1_k24.cpp

fat_tree_topology_6to1_hybrid.o: fat_tree_topology_6to1_hybrid.cpp fat_tree_topology_6to1_hybrid.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c fat_tree_topology_6to1_hybrid.cpp

clean:	
	rm -f *.o htsim_ndp* htsim_tcp* htsim_dctcp*
