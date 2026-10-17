# SIMD 性能测试

## 自动调度、热点原语与 DMA/AHB

2026-10-17。本轮以 `306d06e` 为实现基线；前后使用相同的新基准程序，
基线也加入 Android DMA 导入的构建支持，以便在相同资源上比较。
每项至少预热 50 ms，记录九组样本，按 before / after / after / before 顺序重复。
以下数字均为合并 18 组原始样本的中位数，单位在各表中注明。

### 实现变化

- 图像 Automatic 按行宽筛选：小于 16 像素用 Scalar，16–31 像素排除 AVX2。
  新增显式 `tuneCpuConversion`，实测选优后复用计划；接口和预算规则见 [使用说明](simd.md)。
- SSE2/AVX2 的 32/64 位比较直接提取 lane mask；NEON 优化 32 位 mask、压缩写入及 i32/u32 → f32。
  过滤仍保持顺序且只写入选中的元素，整数转浮点保持原有数值契约。
- 新增 VA、真实 DMA heap、AHB BLOB、原生 RGBA AHB 输出的 CPU 资源基准。
  Android 现可通过 `DmaBufMemory::import` 使用已有的线性 CPU 映射路径。

### 热点原语结果

262144 个元素，过滤约 50% 命中，单位 µs/调用。

| 用例 | 后端 | 优化前 | 优化后 | 加速比 |
| --- | --- | ---: | ---: | ---: |
| copy_if i32 | PC AVX2 | 218.867 | 149.232 | 1.47× |
| copy_if i32 | Android NEON | 768.086 | 612.873 | 1.25× |
| i32 → f32 | Android NEON | 67.237 | 42.511 | 1.58× |
| i32 → f32，未改原生路径 | PC AVX2 | 24.874 | 24.892 | 1.00× |

PC 过滤基线两轮分别为 195.275 / 237.202 µs，优化后为 148.756 / 151.816 µs；
两轮都有收益，但基线波动较大。NEON 过滤两轮为 768.125 / 767.787 → 612.882 / 612.135 µs。
保留的优化有指令证据：x86 使用 movemask，NEON 使用向量 mask 归约、固定长度存储和 scvtf。

### 调度结果与成本

PC 小图，`prepared_run`，单位 µs/帧。计时包含视图校验；这些用例中固定开销占比较高。

| 转换与尺寸 | 原 Automatic | 新 Automatic | 后端变化 |
| --- | ---: | ---: | --- |
| UYVY → NV12，2×5 | 0.465 | 0.467 | AVX2 → Scalar |
| UYVY → NV12，16×5 | 0.479 | 0.469 | AVX2 → SSE2 |
| NV12 → RGBA8，2×5 | 0.455 | 0.475 | AVX2 → Scalar |
| NV12 → RGBA8，16×5 | 0.546 | 0.514 | AVX2 → SSE2 |

短行规则避免选择无法进入向量循环的后端，但本轮没有显示所有短行都变快：
2×5 RGBA 用例约慢 4.4%，16×5 约快 5.8%。Android 对应小图基本持平。

1920×1536 UYVY → NV12，PC VA 资源的两轮调优都选择 Scalar：
同一优化版本的 Automatic 总耗时为 183.568 µs，调优后为 165.354 µs，约快 1.11×。
一次调优花费 3.409 / 3.813 ms，按此次差值约需复用 200 帧才能摊平成本。
HostFrame 基准的两轮则分别保留 AVX2、选择 Scalar；第二轮调优后为 197.682 µs，
比同轮 Automatic 的 183.794 µs 慢 7.6%。VA 分平面用例第二轮选 SSE2 后也慢 2.8%。
调优选择依赖校准时的布局和运行状态，本轮未显示普遍收益；不能据此写死大图后端。

1920×1536 NV12 → RGBA8 的调优保持 PC AVX2 / Android NEON，未带来稳定收益。
默认 100 ms 为软预算，此用例实测约 105–116 ms，原因是已开始的 Scalar 候选转换必须完成。
因此调优应由调用方显式执行一次，普通 `prepare/run` 不承担这项成本。

### 资源端到端结果

1920×1536，平面共用同一对象，Automatic，单位 ms/帧。
UYVY 输入 / NV12 输出首平面 stride 为 3840 / 1920 字节；NV12 输入 / RGBA 输出为 1920 / 7680 字节。

| 资源 | 转换 | 总耗时前 | 总耗时后 | 持有映射后计算 | 空映射与结束访问 |
| --- | --- | ---: | ---: | ---: | ---: |
| PC VA | UYVY → NV12 | 0.2013 | 0.1836 | 0.1827 | 0.0003 |
| PC VA | NV12 → RGBA8 | 1.7056 | 1.7030 | 1.7135 | 0.0002 |
| Android DMA | UYVY → NV12 | 0.3295 | 0.3316 | 0.1560 | 0.1018 |
| Android DMA | NV12 → RGBA8 | 1.6381 | 1.6248 | 1.3059 | 0.1467 |
| Android AHB BLOB | UYVY → NV12 | 0.1650 | 0.1690 | 0.1600 | 0.0046 |
| Android AHB BLOB | NV12 → RGBA8 | 1.3112 | 1.3112 | 1.3059 | 0.0046 |
| Android 原生 RGBA AHB 输出 | NV12 → RGBA8 | 1.3135 | 1.3135 | 1.3060 | 0.0064 |

“总耗时”为 `executor_run`，含映射、计算、同步、Completion 和映射析构。
“持有映射后计算”为 `mapped_run`，仍含视图校验；“空映射与结束访问”为 `map_finish`，不触碰像素。
后两列来自优化版本，是独立测量范围，不能相加还原总耗时，也不能相减推算脏缓存刷新成本。

Android 大图所有 Automatic 和 tuned 都选择 NEON，转换内核本轮未改动，未测得稳定端到端收益。
DMA UYVY 两轮为 0.3273 / 0.3353 → 0.3327 / 0.3234 ms，变化方向不一致；
AHB UYVY 在本次采样中约慢 2.4%，同后端 tuned 数据亦有波动，不归因于调度收益。
PC VA 的 Automatic 始终为 AVX2，UYVY 基线两轮 0.2134 / 0.1877 ms 的差异也说明了状态影响。

这些数据反复使用同一组已分配 buffer，衡量 CPU 稳态性能。
分配、导入、调优、填充及验证均在计时外，没有在途生产者或 GPU fence；
不是摄像头/解码器到显示的整条流水线延迟。原生 RGBA 用例的 NV12 输入仍为 AHB BLOB。
PC 没有可访问的 DMA heap，真实 DMA 数据来自 Android `/dev/dma_heap/system`，没有使用 memfd 模拟。

### 环境、校验与复现

- PC：i7-13700KF，GCC 13.3，Release、C++17，仅固定基准进程到 CPU 2。
- Android：Xiaomi 25091RP04C，SM8750P，Android 16/API 36；NDK r28、目标 API 29、arm64，系统调度。
- 均单线程，不修改系统调频策略。每进程内部预算 45 秒，外部超时 50 秒。
- 每项计时前用不同于参考值的数据覆盖输出并独立校验；结束后直接校验最后一次计时留下的输出和返回值。
  过滤同时检查有效前缀和未写入尾部。此验证不声称逐个迭代检查。
- 24 次完整运行、2256 组用例、20304 组计时样本通过校验。
  覆盖 4096/4099/262144 元素、0/50/100% 命中率、短行、奇高、padding，以及平面共用/分离对象。
- Linux 回归 54 项通过，1 项因无 DMA heap 跳过；Sanitizer 19 项通过。
  Android 八项运行检查、六项编译拒绝检查及安装后 SDK 接入通过；真实 DMA/AHB 用例逐项验证。

[环境、编译命令和 SHA-256](data/simd/dispatch-resources/environment.json) ·
[汇总 CSV](data/simd/dispatch-resources/results.cases.csv) ·
[全部样本 CSV](data/simd/dispatch-resources/results.samples.csv)

原始 JSONL 位于 [dispatch-resources](data/simd/dispatch-resources)。重新测量示例：

```sh
cmake --preset release -DLUTILS_BUILD_BENCHMARKS=ON
cmake --build --preset release --target simd_benchmark simd_resource_benchmark -j2
timeout 50s taskset -c 2 ./build/release/benchmarks/simd/simd_benchmark > build/release/simd.jsonl
timeout 50s taskset -c 2 ./build/release/benchmarks/simd/simd_resource_benchmark --memory va > build/release/va.jsonl
uv run --no-project tools/simd/report.py build/release/simd.jsonl build/release/va.jsonl --output build/release/simd-report

uv run --no-project tools/android/run.py --serial "$ANDROID_SERIAL" --timeout 50 -- \
    build/android-arm64/benchmarks/simd/simd_resource_benchmark --memory dma
```

Android 构建须开启 `LUTILS_BUILD_BENCHMARKS`；将 `--memory` 改为 `ahb` 或 `ahb-rgba` 测量对应资源。
依照上述前后顺序各运行两轮，保留每次 JSONL；根据机器实际允许的 CPU 选择 `taskset` 编号。
在当前源码的独立副本中应用 [before.patch](data/simd/dispatch-resources/before.patch)：
`git apply --unidiff-zero docs/data/simd/dispatch-resources/before.patch`，再只构建上述两个 benchmark 目标，
即可重建本轮实现基线。补丁移除新调优接口，所以基线没有 tuned 项；计时程序与 Android DMA 支持保持相同。

## 上一轮：通用原语与图像转换

以下保留 `306d06e` 时的测量记录，使用上一版预热方法与基线；数值不与本轮直接混算。

2026-10-17，单线程、Release、C++17。每项九组样本，每个版本重复两轮；以下耗时为合并 18 组样本的中位数。
全部输出先用不同于参考值的数据覆盖，再运行当前后端并独立校验。

### 图像转换结果

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

### 上一轮优化收益

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

### 环境和数据

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

### 复现

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

需要复现上一轮通用原语基线时，将 `306d06e` 的源码导出到独立目录，在副本中应用上述补丁，再用相同工具链和配置构建。
该补丁只用于这版源码的性能对照，不应应用到正在使用的工作区。

CI 的 `simd.benchmark.smoke` 使用小输入验证输出与执行流程，不设置跨机器耗时阈值。
