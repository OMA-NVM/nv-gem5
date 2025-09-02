#!/bin/bash

mkdir -p cim_and/bin/arm/linux
# arm-linux-gnueabihf-g++ -std=gnu++17 -O2 -static \
#   -I cim_and/include \
#   -I /home/kaiii/NVM_Simulation/simulator/gem5/include \
#   cim_and/src/cim_and.cc \
#   cim_and/src/cim_api.cpp \
#   -o cim_and/bin/arm/linux/cim_and \
#   -DinCIM=0 \

arm-linux-gnueabihf-g++ -std=gnu++17 -static \
  cim_and/src/simple_test.cc \
  -o cim_and/bin/arm/linux/simple_test \
