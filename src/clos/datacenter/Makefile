CC=g++
CXX=g++
CFLAGS=-Wall  -O0 -ggdb
FF_HOME?=/home/weiyangw/FlexFlow
CRT=`pwd`
INCLUDE= -I/$(CRT)/.. -I$(CRT) -I$(FF_HOME)/protobuf/src
#-I$(CRT)/ksp -I$(CRT)/ksp/boost
LIB=-L.. -L$(FF_HOME)/protobuf/src/.libs -lprotobuf
#-Lksp
OBJS=../eventlist.o ../tcppacket.o ../pipe.o ../queue.o ../queue_lossless.o ../queue_lossless_input.o ../queue_lossless_output.o ../ecnqueue.o ../tcp.o ../dctcp.o ../mtcp.o ../loggers.o ../logfile.o ../clock.o ../config.o ../network.o ../qcn.o ../exoqueue.o ../randomqueue.o ../cbr.o ../cbrpacket.o ../sent_packets.o ../ndp.o ../ndppacket.o ../eth_pause_packet.o ../compositequeue.o ../prioqueue.o ../cpqueue.o ../compositeprioqueue.o ../switch.o ../fairpullqueue.o ../route.o ../ffapp.o ../taskgraph.pb.o

all: htsim_tcp_fc htsim_tcp_fattree htsim_tcp_os_fattree

htsim_tcp_fc: main_tcp_fc.o firstfit.o ../libhtsim.a fc_topology.o
	$(CC) $(CFLAGS) $(INCLUDE) firstfit.o main_tcp_fc.o fc_topology.o $(OBJS) $(LIB) -lhtsim -o htsim_tcp_fc

htsim_tcp_fattree: main_tcp_fattree.o firstfit.o fat_tree_topology.o
	$(CC) $(CFLAGS) $(INCLUDE) firstfit.o main_tcp_fattree.o fat_tree_topology.o $(OBJS) $(LIB) -lhtsim -o htsim_tcp_fattree

htsim_tcp_os_fattree: main_tcp_os_fattree.o firstfit.o os_fattree.o
	$(CC) $(CFLAGS) $(INCLUDE) firstfit.o main_tcp_os_fattree.o os_fattree.o $(OBJS) $(LIB) -lhtsim -o htsim_tcp_os_fattree

main_tcp_fc.o: main_tcp_fc.cpp 
	$(CC) $(INCLUDE) $(CFLAGS) -c  main_tcp_fc.cpp

main_tcp_fattree.o: main_tcp_fattree.cpp 
	$(CC) $(INCLUDE) $(CFLAGS) -c  main_tcp_fattree.cpp

main_tcp_os_fattree.o: main_tcp_os_fattree.cpp 
	$(CC) $(INCLUDE) $(CFLAGS) -c  main_tcp_os_fattree.cpp

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

os_fattree.o: os_fattree.cpp os_fattree.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c os_fattree.cpp

clean:	
	rm -f *.o htsim_ndp* htsim_tcp* htsim_dctcp*
