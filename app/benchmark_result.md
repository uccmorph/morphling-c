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


## 2022.2.10
WSL measure the processing time including recv, handle and send:
```
  int flag = EV_READ | EV_PERSIST;
  event *ev_service = event_new(
      m_base, m_service_socket, flag,
      [](evutil_socket_t fd, short what, void *arg) {
        UDPServer *server = (UDPServer *)arg;
        LOG_F(INFO, "new client event");
        if (what & EV_READ) {
          g_gauge_client_msg.set_probe1();

          sockaddr_in addr;
          MessageHeader header = server->recv_header(fd, addr);

          std::unique_ptr<Transport> trans = std::make_unique<UDPTransport>(fd, addr);

          if (header.type != MessageType::MsgTypeClient &&
              header.type != MessageType::MsgTypeGetGuidance) {
            LOG_F(ERROR, "can not handle msg type %d from clients", header.type);
          }
          server->replica->handle_message(header, trans);

          g_gauge_client_msg.set_probe2();
        }
      },
      this);
  event_add(ev_service, nullptr);
```
[Client op] 29970 points, average time: 13.991358 us
[Client op] 20042 points, average time: 14.770881 us
./client2 --total 12 --group 2 --ro --replicas 127.0.0.1,127.0.0.1,127.0.0.1 --vs 1000 --nums 50000 -v ERROR


[Client op] no probe points in these time
[Client op] no probe points in these time
[Client op] 22527 points, average time: 5.148888 us
[Client op] 27477 points, average time: 5.146486 us
./client2 --total 4 --group 2 --ro --replicas 127.0.0.1,127.0.0.1,127.0.0.1 --vs 1000 --nums 50000 -v ERROR


Adding send gauge:
```
void UDPTransport::send(uint8_t *buf, uint64_t size) {
  g_gauge_send.set_probe1();
  int send_n = sendto(m_fd, buf, size, 0, (sockaddr *)&m_addr, sizeof(sockaddr_in));
  if (send_n == -1) {
    LOG_F(FATAL, "udp send error: %d, %s", errno, std::strerror(errno));
  }
  assert(send_n == (int)size);
  LOG_F(INFO, "send send_n = %d bytes", send_n);
  g_gauge_send.set_probe2();
}
```
[Client op] 34408 points, average time: 5.188735 us
[udp send] 34408 points, average time: 3.109219 us
[Client op] 15596 points, average time: 5.180303 us
[udp send] 15598 points, average time: 3.107129 us
./client2 --total 4 --group 2 --ro --replicas 127.0.0.1,127.0.0.1,127.0.0.1 --vs 1000 --nums 50000 -v ERROR

So we can say the average handle time is 2 us

## 2022.2.28
WSL

ucc@desktop-8sjudi:~/code/morphling-c/build$ ./client-run.sh
total time: 403.931200 ms, throughput: 74.270074 Kops
ucc@desktop-8sjudi:~/code/morphling-c/build$ ./client-run.sh
total time: 396.902900 ms, throughput: 75.585238 Kops
./client2 --total 12 --group 4 --ro --replicas 127.0.0.1,127.0.0.1,127.0.0.1 --vs 1000 --nums 30000 -v ERROR

When we change --group or --total, throughput will drop.
like:
ucc@desktop-8sjudi:~/code/morphling-c/build$ ./client-run.sh
total time: 901.464700 ms, throughput: 33.279173 Kops
ucc@desktop-8sjudi:~/code/morphling-c/build$ ./client-run.sh
total time: 939.145000 ms, throughput: 31.943949 Kops
./client2 --total 12 --group 6 --ro --replicas 127.0.0.1,127.0.0.1,127.0.0.1 --vs 1000 --nums 30000 -v ERROR


## 2022.3.28
PCL VM
replica machine: 8 core, 3 replicas
client machine: 16 core, 2 machines

There are case that GetGuidance will be dropped, see port 9996 on replica0
yijian@kvs-server1:~$ ss -m -ua
State     Recv-Q    Send-Q       Local Address:Port         Peer Address:Port   Process
UNCONN    0         0            127.0.0.53%lo:domain            0.0.0.0:*
         skmem:(r0,rb212992,t0,tb212992,f4096,w0,o0,bl0,d0)
UNCONN    0         0                  0.0.0.0:9990              0.0.0.0:*
         skmem:(r0,rb212992,t0,tb212992,f4096,w0,o0,bl0,d0)
UNCONN    0         0                  0.0.0.0:9993              0.0.0.0:*
         skmem:(r0,rb212992,t0,tb212992,f4096,w0,o0,bl0,d0)
UNCONN    0         0                  0.0.0.0:9996              0.0.0.0:*
         skmem:(r0,rb212992,t0,tb212992,f4096,w0,o0,bl0,d176294)
UNCONN    0         0                  0.0.0.0:53986             0.0.0.0:*
         skmem:(r0,rb212992,t0,tb212992,f0,w0,o0,bl0,d0)
UNCONN    0         0                  0.0.0.0:40495             0.0.0.0:*
         skmem:(r0,rb212992,t0,tb212992,f0,w0,o0,bl0,d0)


./client2 --total 40 --group 2 --ro --ur --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 200000 -v ERROR
And, unreplicated case and achieve higher throughput:
2 machine, 110 Kops

./client2 --total 40 --group 2 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 200000 -v ERROR
Morphling can only achieve roughly 95 Kops.
I'm not sure it's caused by udp dropping packets.

Now the average handling time for operations can reach 10us, 100Kops is a reasonable throughput.
Since if we run --ur cmd only on one client machine, the single machine measured throughput can be 70Kops,
while in two-machine case each can donate 50Kops.
So the limitation of one machine is almost reached.

However, I have some new findings.
When I try to make client machine at low loading, like
./client2 --total 6 --group 3 --ro --replicas 10.1.6.233,10.1.6.234,10.1.6.235 --vs 1000 --nums 50000 -v ERROR
then throughput becomes:
yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
[] 1 points, average time: 1916680.000000 us
[] 1 points, average time: 1916705.000000 us
total time: 1917.314920 ms, throughput: 26.078136 Kops
yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
[] 1 points, average time: 1950148.000000 us
[] 1 points, average time: 1950219.000000 us
total time: 1950.787711 ms, throughput: 25.630672 Kops

multi-client-run is exactly doubling the single-case, to 52Kops:
yijian@kvs-backup:~/morphling-c/build$ ./multi-client-run.sh
[] 1 points, average time: 1776194.000000 us
[] 1 points, average time: 1776365.000000 us
total time: 1776.894731 ms, throughput: 28.138977 Kops
yijian@kvs-backup:~/morphling-c/build$ [] 1 points, average time: 2063745.000000 us
[] 1 points, average time: 2063722.000000 us
total time: 2064.372459 ms, throughput: 24.220436 Kops

And for unreplicated case, throghput becomes:
yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
[] 1 points, average time: 3116557.000000 us
[] 1 points, average time: 3116682.000000 us
total time: 3117.269371 ms, throughput: 16.039679 Kops
yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
[] 1 points, average time: 2831554.000000 us
[] 1 points, average time: 2831630.000000 us
total time: 2832.311392 ms, throughput: 17.653426 Kops

multi-client-run is also doubling, to 32Kops:
yijian@kvs-backup:~/morphling-c/build$ ./multi-client-run.sh
[] 1 points, average time: 3113913.000000 us
[] 1 points, average time: 3114059.000000 us
total time: 3114.541468 ms, throughput: 16.053727 Kops
yijian@kvs-backup:~/morphling-c/build$ [] 1 points, average time: 3160747.000000 us
[] 1 points, average time: 3160848.000000 us
total time: 3161.342327 ms, throughput: 15.816066 Kops

And Raft,
yijian@kvs-backup:~/morphling-c/build$ ./client-run.sh
[] 1 points, average time: 5744718.000000 us, min: 5744718.000000 us, max: 5744718.000000 us
[] 1 points, average time: 5744993.000000 us, min: 5744993.000000 us, max: 5744993.000000 us
total time: 5745.440613 ms, throughput: 5.221532 Kops

Raft is one third of unreplicated case.
Also, raft has longer latency. 380us
Morphling 170us
unreplicated 110us