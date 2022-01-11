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

