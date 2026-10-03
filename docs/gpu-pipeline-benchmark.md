# UYVY → NV12：显存传输与流水线

2026-10-03，在 RTX 5070 Ti 上完成 device-local storage、staging、执行槽复用和单队列流水线。
用户提供的 30 帧 1920×1536 UYVY，循环十轮，所有输出逐字节匹配独立参考。

## 结果与使用选择

| 配置 | 完整吞吐（帧/秒） | 每帧延迟中位数（ms） | P95（ms） |
| --- | ---: | ---: | ---: |
| GPU，显存、复用、深度 1 | 593.96 | **1.665** | 1.777 |
| GPU，显存、复用、深度 2 | **660.13** | 2.756 | 4.210 |
| 当前串行 CPU 参考后端，同样深度 2 | 89.11 | 22.380 | 22.772 |

需要尽早取得单帧结果时用深度 1；连续处理时，本机深度 2 吞吐高约 11%，代价是延后收取结果。
GPU 双帧吞吐约为当前 CPU 参考实现的 7.4 倍；CPU 尚未做 SIMD 或多线程优化。
本次重跑上一阶段保存的可执行文件，90 个单帧样本中位数为 2.639 ms；
新单帧路径为 1.665 ms，降低约 37%。旧单帧基准逐帧验算，新基准整轮验算，
该对照有调度与缓存状态差异；两种实现的原始记录均保留。

设备、驱动、工具链与输入格式沿用 [第一阶段环境](gpu-benchmark.md#环境与输入)：
RTX 5070 Ti / 595.91.07，i7-13700KF，GCC 13.3 Release，C++17，local size 32×1×1。
显存配置要求 Vulkan DEVICE_LOCAL，staging 要求 HOST_VISIBLE；没有绑定独立传输队列。

## 计时口径

输入文件预先读入内存；计划、各槽的输入/输出设备帧、30 个主机输出提前创建。
预热整套 30 帧一轮，再测十轮，共 300 个样本。正式性能关闭验证层和时间戳，
各配置顺序运行，期间不同时构建或运行其他测试；未绑核、未锁频。

整轮从第一帧打包前计时，到最后一帧回读、解包并释放本次收取句柄后结束。
包括每帧主机打包、staging 复制、上传、转换、回读、主机解包、提交和完成处理，
以及流水线填充、排空。吞吐为总帧数除以全部轮次总时间。
逐帧延迟从取得空槽开始打包，到结果解包后结束；包括提交后的等待和延后收取，
不含外部输入排队或等待空槽的时间。P95 使用 nearest-rank。

每轮计时前用固定字节填充主机输出，计时后验算全部结果：Y 原样保留，
UV 相邻行 `(a+b+1)/2`。文件 I/O、初始化、输出填充、oracle 校验不在计时区间。
时间戳诊断单独执行；开启后 finish 会读取 query，因此查询读取开销属于主机等待。
设备时间包含整条 upload/dispatch/readback 及屏障，不能与旧报告的仅 dispatch 区间直接比较。

## 分步对照

以下每项为 90 帧探索测量；最终深度 1/2 使用上表的 300 帧复测。

| Storage / 资源策略 / 深度 | 吞吐（帧/秒） | 延迟中位数（ms） |
| --- | ---: | ---: |
| Host-cached / 每帧创建 / 1 | 188.24 | 5.268 |
| Device-local / 每帧创建 / 1 | 247.39 | 3.983 |
| Host-cached / 复用 / 1 | 310.04 | 2.978 |
| Device-local / 复用 / 1 | 573.85 | 1.673 |
| Device-local / 复用 / 2 | 661.84 | 2.736 |
| Device-local / 复用 / 3 | 559.12 | 5.018 |
| Device-local / 复用 / 4 | 613.18 | 5.839 |

这些路径都使用一次提交和 staging。每帧创建原生资源成本明显；单独启用显存不足以
达到当前结果。深度继续增加没有稳定收益，因此未把更多缓冲设置为推荐配置。
Host-cached 的新 staging 路径也没有超越旧版直接映射路径；内存选项仍需按工作负载测量。

正式 GPU 测量中，预热后和结束时的累计创建计数均为：执行槽 1、描述符池 1、staging buffer 2。
单元测试另验证描述符扩容和缓存上限为零时的释放行为。
深度 2 表示最多保留两个尚未收取的帧结果；驱动可能已完成任务，submit 的 reap 会提前保存结果。
本次只需一个原生执行槽即可处理负载，不能据此宣称 GPU 同时执行两帧。
当前收益来自传输/资源策略和主机调度；未测量或承诺 DMA 与计算引擎重叠。

## 内存与接口边界

每槽 storage 的有效容量为 10,321,920 字节（约 9.84 MiB）；深度 2 共约 19.69 MiB。
本次复用的一组上传/回读 staging 也为约 9.84 MiB。
30 个主机输出共 132,710,400 字节（约 126.56 MiB），另有输入、打包数据和回读快照。
这些是有效载荷容量，不含驱动分配对齐或内部开销。

默认最多四个未完成提交，达到上限时 submit 等待最早提交；默认缓存最多 64 MiB 的
空闲 staging 有效容量。在途 staging 和用户保留的快照在缓存上限之外。
原生资源计数稳定不代表零分配，每帧仍创建主机 vector 和提交描述。
同一 Device 及其 Completion 的调用需要统一串行化；具体契约见
[设计文档](compute-design.md#执行与所有权)。

## 验证与复现

- Release：28/28；Clang ASan/UBSan/LeakSanitizer：28/28；默认可选依赖关闭：11/11。
- 显存路径集成测试覆盖负 stride、padding、奇高、空图、工作组尾部和转换链。
- 异步测试覆盖深度 1/4、逆序收取、重复提交、快照隔离、资源提前释放、Device 提前销毁、
  旧 Completion 读取、描述符扩容、禁止复用和 staging 缓存为零。
- RTX 5070 Ti 的集成、异步和 30 帧深度 4 流水线测试均启用同步验证，资源销毁后
  **0 errors / 0 warnings**。30 帧不能整除深度 4，覆盖尾部排空。

```sh
./build/compute-release/benchmarks/uyvy_nv12_benchmark vulkan 1920 1536 gpu-pipeline.json \
  --require-hardware --device-local --pipeline-depth 2 --rounds 10 \
  build/uyvy-30/input/{1..30}.uyvy
```

深度 1 测试改为 `--pipeline-depth 1`；资源创建对照加 `--no-reuse`。
回归与构建命令见 [README](../README.md#硬件验证与性能基准)。
本地原始记录位于忽略目录 `build/gpu-pipeline/`：

- [GPU 深度 1](../build/gpu-pipeline/gpu-d1-final.json)、[GPU 深度 2](../build/gpu-pipeline/gpu-d2-final.json)、
  [CPU 对照](../build/gpu-pipeline/cpu-d2-final.json)。
- [同步验证](../build/gpu-pipeline/gpu-validation-d4.json)、[旧可执行文件复测](../build/gpu-pipeline/baseline-repeat.json)。
- `host-d1-no-reuse.json`、`host-d1-reuse.json`、`local-d1-no-reuse.json`、`local-d1.json` 至
  `local-d4.json` 保存分步对照。

Windows、其他 GPU 和多队列传输仍需各自验证；这些桌面测试结果不构成实时延迟保证。
