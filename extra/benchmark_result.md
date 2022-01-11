## 2020.1.11
Close all print, servers are 10.1.6.233, 10.1.6.234 and 10.1.6.235

Note that this server cluster is a replication system, as 10.1.6.233 is the main node.
```
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 3
msg: 48000, total time: 950.924722 ms, throughput: 50.477182 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 4
msg: 64000, total time: 1004.627749 ms, throughput: 63.705188 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 5
msg: 80000, total time: 914.594932 ms, throughput: 87.470417 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 6
msg: 96000, total time: 1039.621347 ms, throughput: 92.341313 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 7
msg: 112000, total time: 985.516993 ms, throughput: 113.645935 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 8
msg: 128000, total time: 1036.955784 ms, throughput: 123.438243 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 9
msg: 144000, total time: 1036.750721 ms, throughput: 138.895491 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 10
msg: 160000, total time: 1038.707934 ms, throughput: 154.037526 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 111
^C
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 11
msg: 176000, total time: 958.790481 ms, throughput: 183.564609 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 12
msg: 192000, total time: 930.092239 ms, throughput: 206.431139 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 13
msg: 208000, total time: 1000.548728 ms, throughput: 207.885927 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 13
msg: 208000, total time: 978.442010 ms, throughput: 212.582859 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 13
msg: 208000, total time: 921.907554 ms, throughput: 225.619151 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 13
msg: 208000, total time: 961.197462 ms, throughput: 216.396743 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 13
msg: 208000, total time: 921.349385 ms, throughput: 225.755835 Kops
yijian@kvs-backup:~/morphling-c/extra$ ./client --lt 2000 --t 8 --ct 14
^C
```

When `ct` become 13, throughput improvement is low.
When `ct` become 14, the program stuck

* However, there is a bug in client.cpp, which can make throughput incorrectly calculated and larger than real. Fixed in commit 2398a13.
* After adding gauge in echo_server.cpp, we find that client message handling time is around 6 us to 50 us. As same requests coming, average handling time decreases.

echo server average processing time, calculating by every 1 second
```
22163 points, average time: 15.300997 us
48567 points, average time: 18.233657 us
50889 points, average time: 17.396431 us
50161 points, average time: 17.526006 us
56311 points, average time: 15.668431 us
50505 points, average time: 17.494723 us
51523 points, average time: 17.178891 us
48778 points, average time: 18.131965 us
50552 points, average time: 17.494441 us
53637 points, average time: 16.450473 us
53701 points, average time: 16.408689 us
55010 points, average time: 16.018378 us
55352 points, average time: 15.945639 us
52780 points, average time: 16.731982 us
58349 points, average time: 15.061098 us
55802 points, average time: 15.772248 us
53483 points, average time: 16.476768 us
54267 points, average time: 16.236847 us
53523 points, average time: 16.479663 us
54135 points, average time: 15.959195 us
54736 points, average time: 16.109964 us
55004 points, average time: 15.999564 us
53461 points, average time: 16.509474 us
58470 points, average time: 15.012759 us
54934 points, average time: 16.035333 us
51738 points, average time: 17.072152 us
53360 points, average time: 16.489487 us
56136 points, average time: 15.689290 us
58589 points, average time: 15.017683 us
53174 points, average time: 16.588163 us
20910 points, average time: 15.729890 us

ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./client --lt 200000 --t 8 --ct 4
msg: 1600000, total time: 29760.765100 ms, throughput: 53.762059 Kops
```

* each second processing 55K msg
* 1000000 us / 16 us = 62.5K
* Such result should be reasonable.

processing time in 2 stage, recv and send, using echo server
```
no probe points in these time
40472 points, average time: 0.013194 us
40472 points, average time: 10.576077 us
40472 points, average time: 13.494416 us
56863 points, average time: 0.013313 us
56863 points, average time: 12.360621 us
56863 points, average time: 15.637269 us
55518 points, average time: 0.015545 us
55518 points, average time: 12.735329 us
55518 points, average time: 15.994128 us
54704 points, average time: 0.016416 us
54704 points, average time: 12.954135 us
54704 points, average time: 16.232798 us
54527 points, average time: 0.007024 us
54527 points, average time: 12.976819 us
54527 points, average time: 16.300548 us
51769 points, average time: 0.011184 us
51769 points, average time: 13.777222 us
51769 points, average time: 17.182484 us
50897 points, average time: 0.008055 us
50897 points, average time: 14.027369 us
50897 points, average time: 17.541741 us
35250 points, average time: 0.008596 us
35250 points, average time: 15.107291 us
35250 points, average time: 18.724965 us
no probe points in these time
no probe points in these time
no probe points in these time

ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./client --lt 50000 --t 8 --ct 4
msg: 400000, total time: 7358.609500 ms, throughput: 54.358096 Kops
```

* obviously, send stage takes more time

After removing following code, average time deminishes
```
  // for (auto &peer : m_peer_info) {
  //   auto &info = peer.second;
  //   std::string ip(inet_ntoa(addr.sin_addr));
  //   auto port = ntohs(addr.sin_port);
  //   if (ip == info.ip && port == info.port) {
  //     dest->peer_id = peer.first;
  //   }
  // }
```

```
13220 points, average time: 0.047731 us
13220 points, average time: 10.683207 us
13220 points, average time: 11.977383 us
66931 points, average time: 0.053861 us
66931 points, average time: 11.662055 us
66931 points, average time: 13.019079 us
66393 points, average time: 0.098565 us
66393 points, average time: 11.638079 us
66393 points, average time: 13.031705 us
71086 points, average time: 0.058000 us
71086 points, average time: 10.815196 us
71086 points, average time: 12.108052 us
66523 points, average time: 0.055590 us
66523 points, average time: 11.660072 us
66523 points, average time: 13.044616 us
78779 points, average time: 0.031519 us
78779 points, average time: 9.664695 us
78779 points, average time: 10.901281 us
37068 points, average time: 0.025494 us
37068 points, average time: 9.672737 us
37068 points, average time: 10.919553 us
no probe points in these time

49361 points, average time: 0.106562 us
49361 points, average time: 10.070035 us
49361 points, average time: 11.421892 us
58489 points, average time: 0.252680 us
58489 points, average time: 13.290448 us
58489 points, average time: 14.882884 us
70868 points, average time: 0.082520 us
70868 points, average time: 10.800474 us
70868 points, average time: 12.218462 us
56119 points, average time: 0.461412 us
56119 points, average time: 12.413871 us
56119 points, average time: 15.364440 us
71193 points, average time: 0.045313 us
71193 points, average time: 10.821570 us
71193 points, average time: 12.188769 us
63538 points, average time: 0.060106 us
63538 points, average time: 12.257405 us
63538 points, average time: 13.665287 us
30432 points, average time: 0.072950 us
30432 points, average time: 10.503713 us
30432 points, average time: 11.813519 us
no probe points in these time
no probe points in these time
no probe points in these time

ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./client --lt 50000 --t 8 --ct 4
msg: 400000, total time: 5657.643600 ms, throughput: 70.700813 Kops
```

* The gap between send stage and recv stage also deminished
* set non-blocking doesn't help at all

If we use 1 client thread, processing time increase unexpectedly. Average calculation loses small and big values.
```
11316 points, average time: 0.730382 us
11316 points, average time: 19.828208 us
11316 points, average time: 22.243284 us
17711 points, average time: 0.816893 us
17711 points, average time: 19.904636 us
17711 points, average time: 22.323245 us
17723 points, average time: 0.771935 us
17723 points, average time: 19.856458 us
17723 points, average time: 22.272019 us
17667 points, average time: 0.771948 us
17667 points, average time: 20.020547 us
17667 points, average time: 22.440143 us
17467 points, average time: 0.761035 us
17467 points, average time: 20.077117 us
17467 points, average time: 22.544684 us
17816 points, average time: 0.739223 us
17816 points, average time: 19.840480 us
17816 points, average time: 22.234621 us
300 points, average time: 0.753333 us
300 points, average time: 19.256667 us
300 points, average time: 21.643333 us

ucc@desktop-8sjudi:~/code/morphling-c/extra$ ./client --lt 100000 --t 1 --ct 1
msg: 100000, total time: 5654.144300 ms, throughput: 17.686142 Kops
```