# SIMD 性能测试

## 向量基础层、掩码组合与逻辑宽度

2026-10-24，以 `be4b908` 为基线，保持 C++17、自主封装。新增 `composition` 基准：
同一数据分别运行库实现和独立手写 intrinsic，用于区分算法收益与封装开销。
两份源码使用当前同一份 harness、相同工具链和编译参数；各 ISA 单独编译，关闭 LTO。

### 实现与契约

- 引入私有编译器向量基础层，覆盖整数 min/max、比较选择、整数归约和通用交织；保留实测更合适的 ISA 特化。
- Mask 保存规范的零/全一 lane，比较结果直接进入 select；查询及压缩写出时才提取紧凑位掩码。
  压缩写出复用一次提取结果。保留 x86 无符号比较的 signed-bias intrinsic，避免编译器生成更多指令。
- 新增 `Vec<T,N,A>`、保留逻辑元素数量的 rebind、静态 permute、concat/extract/split，以及带容量的 MemoryView。
  整数求和保持模运算语义；浮点归约顺序、精确过滤边界及 NaN 语义不变。
- DispatchTable 增加可选 AVX2+FMA 实现槽，按独立 CPU 能力选择。既有 FMA 内核已经能生成融合指令，
  本轮不把新增调度入口计为 FMA 算子加速。

`Mask` 与 `DispatchTable` 布局改变，下游需要整体重新编译。详细接口见 [SIMD 设计](simd.md)。

### 测量方法

PC 为 i7-13700KF、GCC 13.3、逻辑 CPU 2；Android 为 SM8750P、Android 16/API 36、NDK r28c/Clang 19、CPU 7。
不修改调频或设备设置。每组五个 ABBA/BAAB 块，每版十个独立进程；每项预热 50 ms，九组计时样本。
尺寸覆盖 4096/4099/262144 元素，指针偏移一个元素；过滤另测 0/50/100% 命中，图像另测布局与 stride。
每次执行均核对输出和写出边界。单进程执行预算 45 秒。

下表单位为 µs/调用、262144 元素。耗时是十个进程中位数的中位数；加速比是五个块配对比值中位数的倒数。
“通过”表示未触发 5% 退化门槛；波动超过既有 3% 噪声标准时标为“无法确定”，不能据中位数宣称稳定收益。

| 平台 / 算子 | 优化前 | 优化后 | 配对加速比 | 判定 |
| --- | ---: | ---: | ---: | --- |
| PC AVX2 / min_i32 | 156.935 | 39.094 | 3.85× | 无法确定 |
| PC AVX2 / max_u32 | 456.728 | 39.394 | 11.61× | 无法确定 |
| PC AVX2 / select_f32 | 450.028 | 63.973 | 6.98× | 无法确定 |
| PC AVX2 / interleave_f32 | 275.418 | 63.265 | 4.27× | 无法确定 |
| PC AVX2 / deinterleave_f32 | 289.630 | 90.874 | 3.17× | 无法确定 |
| PC AVX2 / reduce_u32_block8 | 23.934 | 14.967 | 1.60× | 通过 |
| Android NEON / min_i32 | 1132.474 | 46.093 | 24.54× | 通过 |
| Android NEON / max_u32 | 1134.193 | 46.539 | 24.44× | 通过 |
| Android NEON / select_f32 | 132.300 | 77.711 | 1.71× | 通过 |
| Android NEON / select_i32 | 102.836 | 77.546 | 1.33× | 通过 |
| Android NEON / reduce_u32_block8 | 26.308 | 21.747 | 1.22× | 通过 |

### 与手写 intrinsic 的差距

Android NEON，262144 元素，单位 µs/调用：

| 算子 | 本轮库实现 | 手写 intrinsic |
| --- | ---: | ---: |
| min_i32 | 46.093 | 46.213 |
| select_f32 | 77.711 | 78.057 |
| interleave_f32 | 81.133 | 60.869 |
| deinterleave_f32 | 130.061 | 63.269 |
| reduce_u32_block8 | 21.747 | 17.856 |
| fma_f32 | 63.287 | 63.358 |

NEON 浮点交织和解交织仍受 Batch 值传递与数组存储边界影响。私有汇编探针显示，
仅替换 shuffle builtin 不能消除逐 lane 搬运；将整条 load/操作/store 改为引用接口才会生成连续 Q 寄存器加载。
本轮保持既定值接口与 constexpr 存储契约，因此没有宣称消除这部分开销。
逻辑固定宽度本轮覆盖正确性；上述性能表测量原生宽度。

### 过滤回归

262144 元素、默认精确写出，单位 µs/调用：

| 平台 / 类型 / 命中率 | 优化前 | 优化后 | 变化 | 判定 |
| --- | ---: | ---: | ---: | --- |
| PC AVX2 / u8 / 50% | 40.461 | 40.542 | +0.1% | 通过 |
| PC AVX2 / u16 / 50% | 70.163 | 69.673 | -1.3% | 通过 |
| PC AVX2 / u32 / 50% | 34.338 | 34.098 | -0.5% | 通过 |
| PC AVX2 / u64 / 50% | 120.837 | 120.084 | -0.8% | 通过 |
| PC SSE2 / u64 / 50% | 591.290 | 643.880 | +8.7% | 退化 |
| Android NEON / u8 / 50% | 250.714 | 242.549 | -4.1% | 通过 |
| Android NEON / u64 / 50% | 844.674 | 837.826 | -0.4% | 通过 |
| Android NEON / u8 / 0% | 13.630 | 15.133 | +11.0% | 退化 |
| Android SCALAR / u8 / 0% | 85.538 | 98.162 | +14.7% | 退化 |

PC 的 23 项确认退化包括 SSE2 18 项和 AVX2 5 项；AVX2 的 5 项均来自允许覆盖模式。
Android 的 26 项包括 Scalar 空掩码 23 项，以及 NEON u8 精确空掩码 3 项。
这些结果采用原有阈值，完整性能门禁未通过。宽掩码减少比较到选择的开销，但并未让所有提取和分支路径都更快。
已消除实证的掩码重复提取、逐 lane 栈搬运及无效寄存器往返；剩余差异仍保留在最终矩阵中。

### 图像与既有原语

两台机器的 primitives 矩阵均无确认退化。1920×1536、预先创建计划后的单帧执行，单位 ms：

| 平台 / 转换 | 优化前 | 优化后 | 判定 |
| --- | ---: | ---: | --- |
| PC AVX2 / uyvy_nv12 | 0.185 | 0.184 | 通过 |
| PC AVX2 / nv12_rgba | 1.702 | 1.702 | 通过 |
| Android NEON / uyvy_nv12 | 0.154 | 0.157 | 通过 |
| Android NEON / nv12_rgba | 1.306 | 1.306 | 通过 |

这些是 CPU 可访问的 HostFrame 测量，不包含 DMA/AHB 端到端计时；Android 资源映射另有正确性回归。

### 完整矩阵与证据

| 平台 / 套件 | 通过 | 退化 | 无法确定 |
| --- | ---: | ---: | ---: |
| linux-composition | 93 | 0 | 27 |
| linux-filters | 153 | 23 | 40 |
| linux-primitives | 207 | 0 | 18 |
| android-composition | 70 | 1 | 1 |
| android-filters | 102 | 26 | 16 |
| android-primitives | 163 | 0 | 5 |

共 945 项：788 项通过、50 项确认退化、107 项无法确定。50 项中有 49 项过滤用例，
另 1 项是 Android 手写 intrinsic 对照组的 4099 元素解交织（约 +6.0%）；其源码相同，仍保留判定。
PC 组合算子的部分中位数明显下降，但波动超过标准，因此表中未把它们标为稳定收益。
两台机器的图像与原有原语套件均无确认退化。没有调宽门槛、删去失败项或把未完成的采集纳入结果。

[汇总与归档校验和](data/simd/vector-foundation/summary.json) ·
[PC 组合算子](data/simd/vector-foundation/linux-composition.json) ·
[PC 过滤](data/simd/vector-foundation/linux-filters.json) ·
[PC 原语与图像](data/simd/vector-foundation/linux-primitives.json) ·
[Android 组合算子](data/simd/vector-foundation/android-composition.json) ·
[Android 过滤](data/simd/vector-foundation/android-filters.json) ·
[Android 原语与图像](data/simd/vector-foundation/android-primitives.json)。

Git 中只保存紧凑报告。原始 JSONL、manifest、stderr、源码补丁、编译参数、汇编探针及测试日志保存在
本地 `.local-artifacts/simd/vector-foundation.tar.gz`；其中也保留中间退化版本与主动中止的诊断采集。
最终报告仅使用 `linux-composition` 与 `linux-release-*`、`android-release-*` 的完整采集。
Linux 组合基准与最终二进制逐字节一致；其 SHA-256 已核对。复现流程见 [配对回归工具](simd-regression.md)。

### 正确性验证

Linux 全量回归 63 项通过，1 项因缺少 DMA heap 跳过；Clang 18 ASan/UBSan/LeakSanitizer 与
关闭编译器向量层的通用回退各 30 项通过。NDK r28 构建和 12 项编译拒绝测试通过；Android 真机
Scalar/NEON、调度、图像、资源映射和调优回归通过。

测试覆盖所有整数宽度、浮点 NaN/正负零/无穷/次正规数、固定宽度 1/3/7/8/9/19/64、
保护页边界、非法静态索引和混合 ISA 编译单元。AVX2 与 AVX2+FMA 测试模板采用内部链接，避免被链接器合并。
浮点比较使用独立的整数排序表与 NaN 分类作为预期值，并从 volatile 输入读取实际测试值。
无库最小复现表明 GCC 13 会错误折叠原先重复同一浮点表达式的断言；复现和编译器输出保留在归档中，
未关闭优化 pass，也未改变 NaN 或正负零契约。

## Review 修复：过滤写出、小预算与配对回归

2026-10-18，以未推送的 `843d1de` 为基线。前后使用相同基准源码、编译参数和数据，
PC 固定 CPU 2，Android 固定 CPU 7。每台机器采五个 ABBA/BAAB 块，每版十个独立进程；
各用例预热 50 ms，再测九组样本。两种写出策略使用各自的函数入口，避免运行时策略分支改变计时内核。

### 接口与正确性

- `copy_if` 默认保留精确写出；第五参 `FilterWrite::AllowOverwrite` 允许覆盖返回数量之后的空间。
  两者都要求容量至少为输入长度，支持精确原位过滤，尾批不会越界。新增 guard page、所有尾长及 move-only predicate 测试。
- x86 的 16 位掩码改用 pack + movemask，NEON 的 8 位用位权归约、64 位直接取 lane。
  AVX2 的 32/64 位精确写出使用 maskstore，允许覆盖模式在完整批次使用整宽写出。
- 小预算按纳秒计算，预热最低时长随预算缩放；模拟时钟覆盖 1/3/4/8/16/19/20/21/23/25/100 ms。
  新增 `CpuTuningOutcome`，区分选优、无候选、无确认收益、预热不稳、阶段预算不足和总预算耗尽。
  预热、筛选、确认已拆开，计时和总预算标志仅在公共入口汇总。
- CI 在同一 job 构建 before/head，使用当前共同 harness，允许内部 TU 与链接命令变化。
  新工具、兼容性及进程清理规则见 [固定硬件回归](simd-regression.md)。ADB push 超时可配置。

### 性能对比

262144 个元素、约 50% 命中，单位 µs/调用。时间为各版本十个进程中位数的中位数，
加速比取五个块 after/before 比值中位数的倒数；两种汇总方式可能略有差异。

**默认精确写出**

| 平台 / 元素 | 优化前 | 优化后 | 配对加速比 | 判定 |
| --- | ---: | ---: | ---: | --- |
| PC AVX2 / u8 | 40.542 | 40.607 | 1.00× | 未超过退化门槛 |
| PC AVX2 / u16 | 103.665 | 69.515 | 1.49× | 未超过退化门槛 |
| PC AVX2 / u32 | 140.446 | 34.257 | 4.63× | 未超过退化门槛 |
| PC AVX2 / u64 | 414.309 | 120.927 | 3.51× | 未超过退化门槛 |
| Android NEON / u8 | 382.719 | 251.150 | 1.51× | 未超过退化门槛 |
| Android NEON / u16 | 387.833 | 393.505 | 0.99× | 未超过退化门槛 |
| Android NEON / u32 | 595.694 | 599.731 | 1.00× | 未超过退化门槛 |
| Android NEON / u64 | 861.224 | 844.206 | 1.03× | 未超过退化门槛 |

**显式允许覆盖**

| 平台 / 元素 | 优化前 | 优化后 | 配对加速比 | 判定 |
| --- | ---: | ---: | ---: | --- |
| PC AVX2 / u8 | 40.554 | 18.059 | 2.25× | 未超过退化门槛 |
| PC AVX2 / u16 | 114.154 | 27.819 | 4.27× | 未超过退化门槛 |
| PC AVX2 / u32 | 173.500 | 26.799 | 6.46× | 波动，无法确定 |
| PC AVX2 / u64 | 413.624 | 72.548 | 5.67× | 未超过退化门槛 |
| Android NEON / u8 | 378.630 | 28.205 | 13.44× | 未超过退化门槛 |
| Android NEON / u16 | 393.927 | 47.064 | 8.35× | 未超过退化门槛 |
| Android NEON / u32 | 620.130 | 125.696 | 4.93× | 未超过退化门槛 |
| Android NEON / u64 | 860.821 | 524.184 | 1.64× | 未超过退化门槛 |

完整矩阵包含 4096/4099/262144 元素、0/50/100% 命中，以及可用的 Scalar/SSE2/AVX2/NEON 后端。
默认精确路径没有确认退化。新选项有 10 项确认退化：PC u8 小输入空掩码约 19–20%；
NEON u32/u64 满命中约 20–31%；NEON u64 小输入 50% 命中约 184%。因此它保持显式选择，
不能把“允许覆盖”理解成所有输入上都更快；这些用例应保留 Exact。

两台机器合计 360 项：298 项通过门槛、10 项确认退化、52 项无法确定。
比较工具对两份完整报告均返回退出码 1，**完整性能门禁并未全绿**；没有调宽门槛或删除退化用例。
PC u32 允许覆盖的大输入虽然中位数较快，波动仍超过门槛，未算作确定收益。
[PC 报告](data/simd/review-fixes/linux-filters-report.json) ·
[Android 报告](data/simd/review-fixes/android-filters-report.json) ·
[汇总](data/simd/review-fixes/summary.json)。

### 保留的实现与限制

AVX2 拼接需要低半区长度，总选中数量仍用独立计数。实验中的“全部复用 LUT”增加了依赖，
还使 GCC 丢失短拷贝长度上界，生成 `rep movsq`；本轮撤回该计数重构，保留原计数实现。
NEON 的 NaN 预处理也保留，本轮未进行移除它的性能对照，未改变既有浮点异常行为。

UYVY 布局探针覆盖 1920×64/1536、输入/输出页内偏移 0/16、UV 相对偏移 0/64，
在相同 buffer 上比较预先创建的 Scalar/SSE2/AVX2 计划，并逐字节验证结果。
补齐输出地址偏移后，复现了后端次序反转。以输入页内偏移 0、UV 紧接 Y 的 1920×1536 为例：

| 输出页内偏移 | Scalar | AVX2 | AVX2/Scalar 块比值中位数 |
| --- | ---: | ---: | ---: |
| 0 字节 | 171.47 µs | 157.59 µs | 0.923 |
| 16 字节 | 165.84 µs | 214.03 µs | 1.298 |

GCC 的 Scalar 路径自身也会自动向量化。地址对齐足以改变优胜后端，但原 VA 采集没有记录实际地址，
不能证明它解释了所有历史差异。`prepare(FrameDesc, FrameDesc)` 不接收地址，且同一计划允许绑定不同 view；
本轮保持 Automatic 的现有选择，不把所有大图统一改为 Scalar。地址敏感的内核优化仍需单独验证。
该短探针用于诊断，不能替代资源端到端回归；结果见 [布局报告](data/simd/review-fixes/uyvy-layout-report.json)。
默认 100 ms 调优预算与选优确认标准保留，本轮不宣称降低原有约 16 ms 的一次性校准成本。

### 验证

Linux 全量回归 57 项通过、1 项因没有 DMA heap 跳过；最终 SIMD 22 项和 ASan/UBSan/LeakSanitizer 19 项通过。
回归工具 30 项离线测试通过，NDK r28 构建与 Android Scalar/NEON、调度、图像、资源、调优测试通过。
源码包验证包含本地归档排除检查。CI 尚未启用，AMD Zen 尚未实测。

原始采集、诊断过程、最终源码补丁与验证日志存放在本地 `.local-artifacts/simd/review-fixes.tar.gz`，
校验和见上述汇总。Git 仅保留报告；未完成的采集没有参与最终统计。

## 稳定调优、扩展原语与固定硬件回归

2026-10-18，以 `ff7fbc7` 为实现基线。前后使用相同的扩展基准程序和 C++17 Release 参数，
每个版本五个独立进程；每项预热至少 50 ms、记录九组样本。以下为五个进程中位数的中位数，
不把 45 组内部样本当作 45 次独立实验。PC 固定 CPU 2，Android 固定 CPU 7，均不修改系统调频。
各 suite 串行执行 before 五轮、after 五轮；VA/AHB 的基线先采集，候选在实现定稿后采集。

### 原语结果

262144 个元素，过滤约 50% 命中，单位 µs/调用。

| 用例 | 后端 | 优化前 | 优化后 | 加速比 |
| --- | --- | ---: | ---: | ---: |
| float → i32，向零舍入 | PC AVX2 | 563.186 | 31.805 | 17.71× |
| float → i32，向零舍入 | Android NEON | 6777.500 | 67.872 | 99.86× |
| copy_if u8 | PC AVX2 | 639.895 | 40.598 | 15.76× |
| copy_if u16 | PC AVX2 | 665.516 | 118.881 | 5.60× |
| copy_if u64 | PC AVX2 | 753.875 | 414.837 | 1.82× |
| copy_if u8 | Android NEON | 736.380 | 370.813 | 1.99× |
| copy_if u16 | Android NEON | 840.130 | 400.219 | 2.10× |
| copy_if u64 | Android NEON | 1155.990 | 876.901 | 1.32× |

浮点转换原来逐元素执行 `long double` 舍入和钳位，现使用原生向量转换，因此收益较大。
SSE2/AVX2/NEON 均覆盖四种舍入方式、NaN/无穷/边界值和调用方舍入环境；
8/16/64 位压缩保持稳定顺序、精确写入及精确原位过滤。
实测发现的 NEON 小批量 u64 和 u16 空/满掩码退化，已通过单元素选择和 16 位 mask 位权归约修正。

### 调优效果与成本

调优增加稳定预热、ABBA/BAAB 配对筛选及独立确认，失败时保留默认。
完整规则见 [SIMD 使用说明](simd.md#算法与调度)。诊断时序和软预算分支有模拟时钟测试。

1920×1536、平面共用同一对象，一次校准的中位耗时，单位 ms：

| 资源 | 转换 | 优化前 | 优化后 |
| --- | --- | ---: | ---: |
| PC VA | UYVY → NV12 | 3.486 | 16.142 |
| PC VA | NV12 → RGBA8 | 104.809 | 26.831 |
| Android DMA | UYVY → NV12 | 8.922 | 6.743 |
| Android DMA | NV12 → RGBA8 | 116.098 | 34.681 |
| Android AHB BLOB | UYVY → NV12 | 8.840 | 6.601 |
| Android AHB BLOB | NV12 → RGBA8 | 115.978 | 37.068 |
| Android 原生 RGBA AHB | NV12 → RGBA8 | 116.081 | 34.547 |

PC VA 的 UYVY 五轮选择两次 SSE2、三次 Scalar；校准后的总运行耗时中位数为 165.006 µs，
Automatic 为 188.561 µs。确认测量增加了约 12.7 ms 的一次性成本。
RGBA 探测到较慢 Scalar 后即跳过完整筛选，避免原来反复运行到总预算上限。
这两种转换使用整数图像内核，上表的浮点原语加速不能直接算作图像吞吐收益。

### 资源端到端结果

1920×1536、Automatic、平面共用同一对象，单位 ms/帧。

| 资源 | 转换 | 优化前 | 优化后 | 判定 |
| --- | --- | ---: | ---: | --- |
| PC VA | UYVY → NV12 | 0.1834 | 0.1886 | 波动，无法确定 |
| PC VA | NV12 → RGBA8 | 1.7040 | 1.7079 | 未超过退化门槛 |
| Android DMA | UYVY → NV12 | 0.2987 | 0.3072 | 未超过退化门槛 |
| Android DMA | NV12 → RGBA8 | 1.6127 | 1.6118 | 未超过退化门槛 |
| Android AHB BLOB | UYVY → NV12 | 0.1638 | 0.1683 | 未超过退化门槛 |
| Android AHB BLOB | NV12 → RGBA8 | 1.3113 | 1.3111 | 未超过退化门槛 |
| Android 原生 RGBA AHB | NV12 → RGBA8 | 1.3134 | 1.3134 | 未超过退化门槛 |

总耗时含映射、计算、同步、Completion 和映射析构，buffer 分配、导入和调优在计时外。
输入和输出是反复使用的新分配 buffer，没有在途生产者、GPU fence 或摄像头/显示设备。
DMA 来自 Android `/dev/dma_heap/system`；原生 RGBA 用例的 NV12 输入为 AHB BLOB。

### 原有路径的回归检查

首次原有原语测试在 PC 检出五项、Android 检出两项退化。反汇编显示，相关 PC Scalar/SSE2
目标文件完全相同，Android i32 → f32 热循环指令也相同，但链接后的循环地址对齐发生了变化。
因此给前后两份共同计时程序的 `micro::execute` 都增加 `noinline, aligned(64)`，固定函数入口对齐。

固定对齐后，PC 225 项中 211 项通过门槛，14 项无法确定，没有确认退化。
Android 仍有 4096 元素、50% 命中的 i32 过滤慢约 5.3%；恢复其原有的 32 位定长部分写入后，
最终 168 项中 162 项通过门槛，6 项无法确定，没有确认退化。
新的 8/16 位路径保留按字节数分解的精确写入，64 位部分掩码直接选择一个元素。

这次控制函数入口对齐支持了链接布局影响先前结果的判断，但没有固定整个程序和查找表的布局。
中间报告、最终报告及指令证据保存在
[布局诊断报告](data/simd/stable-tuning/layout-check)，没有将无法确定的结果算作通过。
原始采集已移入本地归档，位置与校验和见下文。

### 数据与复现

PC：i7-13700KF / GCC 13.3；Android：SM8750P / NDK r28、arm64、目标 API 29。
每份 capture 保存机器、编译命令、链接命令、基准源码哈希、二进制哈希及全部原始 JSONL。
最终八组 suite 共 900 项前后对照：844 项通过门槛、0 项确认退化、56 项因波动或证据不一致而无法确定。
16 份主采集记录的二进制哈希均与最终构建核对一致。
[结果汇总](data/simd/stable-tuning/summary.json) · [报告目录](data/simd/stable-tuning)；
比较门槛、命令和 CI 前置配置见 [固定硬件回归](simd-regression.md)。
原提交的 267 份文件完整备份于本地 `.local-artifacts/simd/843d1de/stable-tuning-original.tar.gz`，
归档内容逐文件核对后，移除了 Git 中的 253 份 JSONL、manifest 和 stderr。
报告保留全部字段，压缩空白以减少行数；归档 SHA-256 见 [归档记录](data/simd/stable-tuning/archive.json)。
该归档未上传，CI 后续通过 job artifact 保存原始采集。

重建基线时，将 `ff7fbc7` 导出到独立源码目录，再复制当前 `benchmarks/simd/` 整个目录作为共同计时程序。
两个版本都用 `Release`、`CMAKE_CXX_STANDARD=17`、`CMAKE_CXX_EXTENSIONS=OFF`、
`CMAKE_EXPORT_COMPILE_COMMANDS=ON`、`LUTILS_BUILD_BENCHMARKS=ON` 配置。
Android 另使用相同 NDK r28、`arm64-v8a`、`android-29`、`c++_static`。
本节历史记录使用 `tools/simd/regression.py` 分别采集、离线比较；
新的 [回归流程](simd-regression.md) 改为同一 job 构建前后版本并交错采集。
初次布局诊断使用 `ff7fbc7` 中原有的 `Kernel.hpp`；最终原有原语测试和 Android 扩展原语测试
使用当前加入入口对齐的共同计时程序。其他 suite 的最终二进制与既有采集记录逐一核对一致。

功能验证：Linux 全量 56 项通过，1 项因没有 DMA heap 跳过；最终 SIMD 21 项通过，
ASan/UBSan/LeakSanitizer 18 项通过，回归工具 18 项离线测试通过。
NDK r28 构建及 Android Scalar/NEON、调度、图像、资源和调优测试通过。
固定硬件 CI 的脚本与 workflow 已就绪，启用仍需专用 runner 和仓库配置，当前保持关闭。

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
