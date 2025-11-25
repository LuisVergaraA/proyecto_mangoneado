#!/bin/bash
set -e
PORT=9000
R=4
X=10
Z=30
W=200
LABEL=200
B=0.05

./build/robots $PORT $R $X $Z $W $LABEL $B &
PID_ROBOTS=$!
sleep 1
./build/vision 127.0.0.1 $PORT 15 $Z 1234

wait $PID_ROBOTS
