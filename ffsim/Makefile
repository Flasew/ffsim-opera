fakesim:
	g++ -O simulator.cc -o fakeffsim
simulator:
	nvcc simulator.cc -lcudnn -lcublas -std=c++11 -arch=compute_61 -code=sm_61
