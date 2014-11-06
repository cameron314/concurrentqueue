#!/bin/bash
g++ ../main.cpp -o test_debug.exe -D_DEBUG -D_XOPEN_SOURCE -Wall -Wno-deprecated -g -O0 -fno-inline
