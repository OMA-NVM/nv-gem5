#!/bin/bash

mkdir -p cim_and/bin/arm/linux
arm-linux-gnueabihf-g++ -std=gnu++17 -O2 -static \
  -I cim_and/include \
  cim_and/src/simple_test.c \
  -o cim_and/bin/arm/linux/simple_test
