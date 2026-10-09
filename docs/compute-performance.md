# 0.2 性能与验收记录

2026-10-09，Linux，i7-13700KF，RTX 5070 Ti（驱动 595.91.07）。
Release 使用 GCC 13.3，内核前端使用 Clang 18。测量在构建和测试结束后单独运行。

## 改动与范围

- 普通类内核由 Device 的持久线程池分段执行；local_size 在生成的 CPU 调用中成为编译期常量。
  执行范围等组信息每个 worker 设置一次，避免在每个像素重复计算。
- Vulkan 回读结果按容量预算缓存。只有所有外部持有者均释放后才能复用 vector，省去稳定阶段的大块分配和清零。
- 保留同一份 C++ 算法及串行参考路径。GPU 的计算内核、采样规则、整数结果均未改变。

UYVY → NV12 仍是 O(width × height) 的逐像素算法，帧槽数量决定驻留空间。
本次没有加入独立 SIMD 算法副本或多队列后端；它们需要独立的瓶颈证据和验收，不能由本次吞吐结果推断收益。

## 相同代码版本的对照

使用用户提供的 30 帧紧密 UYVY，1920×1536，深度 2。预热一轮，测量十轮，共 300 帧。
输入每帧 5,898,240 字节，输出每帧 4,423,680 字节。
每轮计时后，全部输出逐字节对比独立 Y 保留、UV 两行舍入平均参考。

| 配置 | 吞吐 fps | 延迟中位数 ms | P95 ms |
| --- | ---: | ---: | ---: |
| CPU，1 worker | 100.91 | 19.759 | 20.142 |
| CPU，默认 8 workers | 312.38 | 5.833 | 8.042 |
| GPU，关闭回读 vector 缓存 | 657.84 | 2.788 | 4.410 |
| GPU，开启回读 vector 缓存 | 755.98 | 2.571 | 2.774 |

本次 CPU 并行配置约为同版本串行配置的 3.10 倍；GPU 缓存提高吞吐约 14.9%。
GPU 两种配置都使用 device-local storage，关闭验证层与时间戳，其他参数相同。
关闭缓存时，回读 vector 累计新建次数从预热后的 30 增至测量后的 330；开启时保持为 2。
两者的执行槽、描述符池、staging buffer 创建次数均保持为 1、1、2。
上传打包与命令描述仍存在分配，这不是零分配接口。

计时包括主机打包、上传、计算、回读、解包及流水线填充/排空；不包括文件 I/O、初始化、参考校验。
延迟从获得空闲槽并开始打包计到主机输出就绪，不含入场等待。没有绑核、锁频或独占系统，数据是本机观测值。

改动前同负载基线为 CPU 105.63 fps、GPU 652.22 fps。新串行调度路径约低 4.5%；
默认并行路径相对旧 CPU 基线约为 2.96 倍。该结果不意味着所有尺寸都适合并行；
`CpuOptions` 的线程数和阈值应按实际负载调整。

## 复现

```sh
./build/compute-release/benchmarks/uyvy_nv12_benchmark cpu 1920 1536 serial.json \
  --cpu-workers 1 --warmup 1 --rounds 10 --pipeline-depth 2 input/*.uyvy
./build/compute-release/benchmarks/uyvy_nv12_benchmark cpu 1920 1536 parallel.json \
  --warmup 1 --rounds 10 --pipeline-depth 2 input/*.uyvy
./build/compute-release/benchmarks/uyvy_nv12_benchmark vulkan 1920 1536 cache-off.json \
  --require-hardware --device-local --no-readback-cache --warmup 1 --rounds 10 --pipeline-depth 2 input/*.uyvy
./build/compute-release/benchmarks/uyvy_nv12_benchmark vulkan 1920 1536 cache-on.json \
  --require-hardware --device-local --warmup 1 --rounds 10 --pipeline-depth 2 input/*.uyvy
```

schema 3 JSON 记录 cpu_workers、缓存开关、逐帧样本、逐轮耗时和四类资源计数。
cpu_workers=0 表示按 CPUOptions 默认策略选择；本机选择 8 workers。
本轮原始报告在 `build/next-validation/isolated-{cpu-serial,cpu-parallel,gpu-off,gpu-on}.json`。
此前的短测和历史报告保留原始条件，不与这组数字拼接比较。

## 验收

| 配置 | 结果 |
| --- | --- |
| 基础 Release | 17/17 |
| 计算 CPU-only | 56/56 |
| 完整 Release，含 Vulkan 与 SDK 下游测试 | 62/62 |
| Clang ASan/UBSan/LSan，Vulkan 使用 Lavapipe | 60/60 |
| CPU 调度器 ThreadSanitizer | 1/1 |
| NVIDIA FP16、工作组、异步快照验证层检查 | 通过，0 errors / 0 warnings |

随后对 CPU 工作组尺寸溢出检查和 float/double 数学行为的修正做了针对性复验，相关 Release 与 sanitizer 测试全部通过。
sanitizer 配置关闭安装，因此比完整 Release 少两个安装/归档测试。
SDK 安装迁移、二进制归档、源码完整构建及源码再次打包均已在本机运行。
新增 GitHub Actions 工作流的远端运行结果尚待实际触发。

本机 NVIDIA 依赖栈的已知 LSan 问题及独立复现见 [此前记录](pdf-parity.md#验证记录)。
本轮泄漏检查显式使用 Lavapipe 并保持 detect_leaks=1；硬件测试单独检查输出和 Vulkan validation。
日志集中在忽略目录 `build/next-validation/`。
