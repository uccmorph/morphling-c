## 2022.1.11

Use commit dc40cb4

Unreplicated read
```
./client2 --total 4 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 2214.727984 ms, throughput: 13.545682 Kops
./client2 --total 6 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1645.614479 ms, throughput: 18.230272 Kops
./client2 --total 8 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1310.579828 ms, throughput: 22.890632 Kops
./client2 --total 10 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1235.642961 ms, throughput: 24.278858 Kops
./client2 --total 12 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1005.363977 ms, throughput: 29.839939 Kops
./client2 --total 14 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1007.071893 ms, throughput: 29.789333 Kops
./client2 --total 16 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1000.845772 ms, throughput: 29.974648 Kops

./client2 --total 36 --group 6 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1063.473304 ms, throughput: 28.209453 Kops
```

Guidance Read
```
./client2 --total 4 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1586.802244 ms, throughput: 18.905948 Kops
./client2 --total 6 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1334.248194 ms, throughput: 22.484572 Kops
./client2 --total 8 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1167.290926 ms, throughput: 25.700534 Kops
./client2 --total 10 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1165.124302 ms, throughput: 25.748326 Kops
./client2 --total 12 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1100.358513 ms, throughput: 27.263841 Kops
./client2 --total 14 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1072.235788 ms, throughput: 27.978921 Kops
./client2 --total 16 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
total time: 1045.016271 ms, throughput: 28.707687 Kops
./client2 --total 24 --group 3 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 30000 -v ERROR
(
  total time: 1045.016271 ms, throughput: 28.707687 Kops
  yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
  total time: 1103.737343 ms, throughput: 27.180380 Kops
  yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
  total time: 1031.321178 ms, throughput: 29.088901 Kops
  yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
  total time: 1131.949700 ms, throughput: 26.502944 Kops
)
```

* Compare to benchmard in `extra`, this app has a ceiling throughput at 29 Kops, far less than `extra`'s replication mode.
* So, transport should not be bottleneck at present, neither read mode.
* The key to improve throughput is how to minimize system overhead on app logic.

server average processing time
```
./client2 --total 12 --group 3 --ro --ur --replicas 127.0.0.1,127.0.0.1,127.0.0.1 --vs 1000 --nums 100000 -v ERROR
23421 points, average time: 36.878784 us
25726 points, average time: 36.483946 us
25962 points, average time: 36.181843 us
24903 points, average time: 35.182348 us
ucc@desktop-8sjudi:~/code/morphling-c/build$ ./client-run.sh
total time: 3852.494500 ms, throughput: 25.957208 Kops
```
* Compare to `extra`'s result, average processing time is larger.

## 2022.1.12

Using tcp server, send time is:
```
[Client op] loop 10, interval: 93.042000 us
[send] loop 60, interval: 8.620000 us
[Client op] loop 11, interval: 53.408000 us
[send] loop 61, interval: 8.415000 us
[Client op] loop 12, interval: 45.425000 us
[send] loop 62, interval: 8.100000 us
[Client op] loop 13, interval: 45.121000 us
[send] loop 63, interval: 9.660000 us
[Client op] loop 14, interval: 48.528000 us
[send] loop 64, interval: 8.240000 us
[Client op] loop 15, interval: 47.453000 us
[send] loop 65, interval: 7.920000 us
[Client op] loop 16, interval: 42.500000 us
[send] loop 66, interval: 7.696000 us
[Client op] loop 17, interval: 48.821000 us
[send] loop 67, interval: 8.548000 us
[Client op] loop 18, interval: 42.147000 us
[send] loop 68, interval: 7.873000 us
[Client op] loop 19, interval: 45.766000 us
[send] loop 69, interval: 7.894000 us

./client2 --total 1 --group 1 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 10 -v ERROR
```

* The first probe of Client op is the point after recv, and the second probe of Client op is the point after send.
* So, message processing takes most of the time.