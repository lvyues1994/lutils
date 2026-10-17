# SIMD 性能测试

2026-10-17，单线程、Release、C++17。每项九组样本，每个版本重复两轮；以下耗时为合并 18 组样本的中位数。
全部输出先用不同于参考值的数据覆盖，再运行当前后端并独立校验。

## 图像转换结果

1920×1536，紧密排列，单位 ms/帧。计时包含准备好的 CPU 计划执行及视图校验。

| 平台 | 转换 | Scalar | SSE2 | AVX2 | NEON |
| --- | --- | ---: | ---: | ---: | ---: |
| PC | UYVY → NV12 | 0.170 | 0.182 | 0.194 | — |
| PC | NV12 → RGBA8 | 14.130 | 2.378 | 1.704 | — |
| Android | UYVY → NV12 | 1.249 | — | — | 0.186 |
| Android | NV12 → RGBA8 | 26.434 | — | — | 1.306 |

Scalar 允许编译器自动向量化。PC 的 UYVY 转换已能由编译器有效向量化，手动选择更宽 ISA 没有带来收益。
Automatic 依照可用 ISA 选择，不代表对每种算法和尺寸都最快；调用方可显式指定 Target。
这里测量 CPU 可访问的合成 buffer，不包含 DMA/AHB 映射、GPU、传输或完整视频流水线。

## 本轮优化收益

基线使用相同算法、同一基准程序，但关闭本轮新增的原生扩宽、缩窄、移位、16 位交织和压缩优化。
基线源码可由 [generic-baseline.patch](data/simd/generic-baseline.patch) 在代码副本中重建；UYVY 的既有原生路径保留。

| 用例 | 后端 | 通用原语 ms | 优化后 ms | 加速比 |
| --- | --- | ---: | ---: | ---: |
| NV12 → RGBA8，1920×1536 | PC AVX2 | 24.948 | 1.704 | 14.6× |
| NV12 → RGBA8，1920×1536 | Android NEON | 20.705 | 1.306 | 15.9× |
| u8 → u16，262144 元素 | PC AVX2 | 0.0612 | 0.0091 | 6.7× |
| u16 → u8 饱和，262144 元素 | PC AVX2 | 0.1022 | 0.0079 | 12.9× |
| copy_if i32，262144 元素、约 50% 命中 | PC AVX2 | 0.7053 | 0.2318 | 3.0× |
| u8 → u16，262144 元素 | Android NEON | 0.0449 | 0.0168 | 2.7× |
| u16 → u8 饱和，262144 元素 | Android NEON | 0.0845 | 0.0142 | 6.0× |
| copy_if i32，262144 元素、约 50% 命中 | Android NEON | 1.0382 | 0.7765 | 1.3× |

两种图像转换均为 O(宽×高)，不分配像素中间缓冲。NV12 → RGBA8 每像素至少读取 1.5 字节、写入 4 字节。
优化减少扩宽、缩窄及重排中的逐 lane 搬运，并在 AVX2/NEON 过滤中用查表重排替代逐元素分支。
未改变颜色公式、舍入、稳定过滤顺序或浮点归约顺序。整数转浮点和归约也纳入基准，没有为所有操作添加手写指令。

## 环境和数据

- PC：Intel Core i7-13700KF，GCC 13.3，固定测试进程到逻辑 CPU 2；SSE2 与 AVX2 分文件编译。
- Android：Xiaomi 25091RP04C，SM8750P，Android 16/API 36；NDK 28.2.13676358、目标 API 29、Clang 19、arm64。
- 不修改调频策略。Android 由系统调度，样本保存执行前后的 CPU 编号；本轮主要位于 CPU 6/7。
- PC 按基线/优化/优化/基线顺序运行，Android 按优化/基线/基线/优化运行。热状态及系统负载仍会影响结果。
- 660 组用例、5940 组原始计时样本均通过校验。每组保存迭代次数、总耗时、CPU 编号、中位数、MAD、最小和最大值。

[环境、编译命令和 SHA-256](data/simd/environment.json) ·
[汇总 CSV](data/simd/results.cases.csv) · [全部样本 CSV](data/simd/results.samples.csv)

原始 JSONL 位于 [data/simd](data/simd)：`pc-*` 和 `android-*` 各四份。
尺寸覆盖 4096/4099/262144 元素、640×480/1920×1536/带 padding 的 642×481；过滤覆盖 0/50/100% 命中率。
图像同时记录 `prepared_run` 与 `prepare_and_run`，可在 CSV 中按 scope 对比。

## 复现

```sh
cmake --preset release -DLUTILS_BUILD_BENCHMARKS=ON
cmake --build --preset release --target simd_benchmark -j2
./build/release/benchmarks/simd/simd_benchmark > build/release/simd.jsonl
uv run --no-project tools/simd/report.py build/release/simd.jsonl --output build/release/simd
```

Linux 可用 `taskset -c <允许使用的CPU>` 仅固定本次基准进程。Android 使用现有隔离 runner：

```sh
uv run --no-project tools/android/run.py --serial "$ANDROID_SERIAL" -- \
    build/android-arm64/benchmarks/simd/simd_benchmark > build/android-arm64/simd.jsonl
```

Android 构建也需开启 `LUTILS_BUILD_BENCHMARKS`。runner 自动创建并清理独立设备目录。
每个基准进程单线程、总预算 45 秒；超时或结果错误以非零状态退出，不输出完成记录。
报告脚本拒绝不完整记录、错误样本数量、重复用例和不一致统计量。

需要复现通用原语基线时，将源代码复制到独立目录，在副本中应用上述补丁，再用相同工具链和配置构建。
该补丁只用于这版源码的性能对照，不应应用到正在使用的工作区。

CI 的 `simd.benchmark.smoke` 使用小输入验证输出与执行流程，不设置跨机器耗时阈值。
