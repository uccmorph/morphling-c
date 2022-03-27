#!/bin/bash

# total clients, how many clients in a thread, all thread send `nums` requests
./client2 --total 1 --group 1 --ro --replicas 127.0.0.1,127.0.0.1,127.0.0.1 --vs 1000 --nums 3 -v 5