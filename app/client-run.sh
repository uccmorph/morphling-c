#!/bin/bash

# total clients, how many clients in a thread, all thread send `nums` requests
./client2 --total 6 --group 3 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR