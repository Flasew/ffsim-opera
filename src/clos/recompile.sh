#!/bin/bash 

make clean && make -j `nproc -all` && make -C datacenter clean && make -C datacenter -j `nproc -all`
