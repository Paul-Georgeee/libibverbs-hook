# Hook RDMA POST SEND ADN POLL CQ

## Post send和Poll cq
需要在`ibv_open_device`返回后替换对应的函数指针：
```c
struct ibv_context *ctx = real_ibv_open_device(device);
real_post_send = ctx->ops.post_send;
ctx->ops.post_send = hook_post_send;
real_poll_cq = ctx->ops.poll_cq;
ctx->ops.poll_cq = hook_poll_cq;
```

这里有一个小坑是libibverbs中为了兼容性提供了两套API，用了GNU symbol versioning机制：
```c
LATEST_SYMVER_FUNC(ibv_create_cq, 1_1, "IBVERBS_1.1",
		   struct ibv_cq *,
		   struct ibv_context *context, int cqe, void *cq_context,
		   struct ibv_comp_channel *channel, int comp_vector);
COMPAT_SYMVER_FUNC(ibv_create_cq, 1_0, "IBVERBS_1.0",
		   struct ibv_cq_1_0 *,
		   struct ibv_context_1_0 *context, int cqe, void *cq_context,
		   struct ibv_comp_channel *channel, int comp_vector);
```
所以在查询对应的函数地址时需要使用`dlvsym`函数，而不是`dlsym`函数：
```c
real_ibv_open_device = dlvsym(handle, "ibv_open_device", "IBVERBS_1.1");
```


## CQE硬件时间戳的支持
CQE硬件时间戳需要在创建CQ时使用`ibv_create_cq_ex`函数，因此需要hook `ibv_create_cq`函数，在hook函数中直接调用`ibv_create_cq_ex`函数，并设置`IBV_CQ_INIT_ATTR_FLAGS_TIMESTAMP`标志就可以：


## 编译和运行
编译
```bash
gcc -shared -fPIC -o libhook.so rdma_hook.c hash_table.c -ldl
```

运行
```bash
LD_PRELOAD=rdma-hook/libhook.so ib_send_bw -d mlx5_0 -n 10 -s 1048576 --report_gbits --use_old_post 10.0.0.14
---------------------------------------------------------------------------------------
                    Send BW Test
 Dual-port       : OFF		Device         : mlx5_0
 Number of qps   : 1		Transport type : IB
 Connection type : RC		Using SRQ      : OFF
 PCIe relax order: OFF
 ibv_wr* API     : OFF
 TX depth        : 10
 CQ Moderation   : 1
 Mtu             : 1024[B]
 Link type       : Ethernet
 GID index       : 3
 Max inline data : 0[B]
 rdma_cm QPs	 : OFF
 Data ex. method : Ethernet
---------------------------------------------------------------------------------------
 local address: LID 0000 QPN 0x01c4 PSN 0xb1dbce
 GID: 00:00:00:00:00:00:00:00:00:00:255:255:172:16:00:18
 remote address: LID 0000 QPN 0x0067 PSN 0xe9e571
 GID: 00:00:00:00:00:00:00:00:00:00:255:255:172:16:00:14
---------------------------------------------------------------------------------------
 #bytes     #iterations    BW peak[Gb/sec]    BW average[Gb/sec]   MsgRate[Mpps]
[INFO]: wr_id=0, data_size=1048576, post_time(software)=193684, complete_time(software)=193783, latency(complete_time - post_time)=99 us, throughput 84.733414 Gbps, difference between the software timestamps of two adjacent CQEs 0 us, hardware timestamp diff 0 ns

[INFO]: wr_id=1, data_size=1048576, post_time(software)=193689, complete_time(software)=193873, latency(complete_time - post_time)=184 us, throughput 45.590261 Gbps, difference between the software timestamps of two adjacent CQEs 90 us, hardware timestamp diff 14156 ns

[INFO]: wr_id=2, data_size=1048576, post_time(software)=193690, complete_time(software)=193964, latency(complete_time - post_time)=274 us, throughput 30.615358 Gbps, difference between the software timestamps of two adjacent CQEs 91 us, hardware timestamp diff 14157 ns

[INFO]: wr_id=3, data_size=1048576, post_time(software)=193690, complete_time(software)=194054, latency(complete_time - post_time)=364 us, throughput 23.045626 Gbps, difference between the software timestamps of two adjacent CQEs 90 us, hardware timestamp diff 14157 ns

[INFO]: wr_id=4, data_size=1048576, post_time(software)=193690, complete_time(software)=194145, latency(complete_time - post_time)=455 us, throughput 18.436501 Gbps, difference between the software timestamps of two adjacent CQEs 91 us, hardware timestamp diff 14158 ns

[INFO]: wr_id=5, data_size=1048576, post_time(software)=193691, complete_time(software)=194235, latency(complete_time - post_time)=544 us, throughput 15.420235 Gbps, difference between the software timestamps of two adjacent CQEs 90 us, hardware timestamp diff 14156 ns

[INFO]: wr_id=6, data_size=1048576, post_time(software)=193691, complete_time(software)=194326, latency(complete_time - post_time)=635 us, throughput 13.210406 Gbps, difference between the software timestamps of two adjacent CQEs 91 us, hardware timestamp diff 14157 ns

[INFO]: wr_id=7, data_size=1048576, post_time(software)=193691, complete_time(software)=194417, latency(complete_time - post_time)=726 us, throughput 11.554556 Gbps, difference between the software timestamps of two adjacent CQEs 91 us, hardware timestamp diff 14156 ns

[INFO]: wr_id=8, data_size=1048576, post_time(software)=193692, complete_time(software)=194507, latency(complete_time - post_time)=815 us, throughput 10.292771 Gbps, difference between the software timestamps of two adjacent CQEs 90 us, hardware timestamp diff 14158 ns

[INFO]: wr_id=9, data_size=1048576, post_time(software)=193692, complete_time(software)=194598, latency(complete_time - post_time)=906 us, throughput 9.258949 Gbps, difference between the software timestamps of two adjacent CQEs 91 us, hardware timestamp diff 14156 ns

Conflicting CPU frequency values detected: 1200.686000 != 2838.312000. CPU Frequency is not max.
 1048576    10               82.33              52.30  		   0.006235
---------------------------------------------------------------------------------------
```