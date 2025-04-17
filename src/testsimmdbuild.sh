#!/bin/bash
# This script builds the test code
gcc -std=c99 -O3 -mavx2 -mfma -o testsimmd testsimmd.c
