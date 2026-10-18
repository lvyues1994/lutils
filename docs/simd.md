# SIMD 模块

`lutils::simd` 是 C++17 通用 SIMD 基础层。Batch、mask、原生指令封装和 CPU 检测由本库实现，
不依赖第三方 SIMD 库。算法通过统一操作组合，再在独立编译单元中生成各 ISA 版本。
这落实了《Advanced SIMD algorithms in practice》中用重排、掩码和压缩组合算法的思路。

## 模块与接入

| CMake 目标 | 内容 |
| --- | --- |
| `lutils::simd` | 头文件：值类型、算术、内存、重排、转换、压缩、归约和算法 |
| `lutils::simd_dispatch` | CPU/OS 能力检测和整段算法的函数指针选择 |
| `lutils::image_simd` | CPU UYVY422 → NV12、NV12 → RGBA8，以及可复用的 CpuConversionPlan |
| `lutils::image_memory` | CpuConversionExecutor：统一映射 VA、线性 DMA、AHB BLOB 和原生 RGBA 输出 |

以上目标随基础构建安装，不需要 kernelc、Vulkan。独立使用头文件链接 `lutils::simd`；
运行时调度另外包含 `<lutils/simd/Dispatch.hpp>` 并链接 `lutils::simd_dispatch`。
图像入口包含 `<lutils/image/SimdConversion.hpp>` 并链接 `lutils::image_simd`。

内部由 `detail/Scalar.hpp` 定义标量语义，`Sse2.hpp`、`Avx2.hpp`、`Neon.hpp` 提供原生操作。
公共算法不接触 intrinsic 类型。`Batch` 使用对齐的固定存储，操作可以内联为寄存器指令；
值类型与基础操作不分配堆内存。图像入口复用 FrameView 的格式、容量和 stride 校验。

## 值与内存契约

`Batch<T, Abi>` 支持 `std::int8_t/uint8_t` 至 `std::int64_t/uint64_t`、`float`、`double`。
`abi::scalar` 为一个元素；SSE2/NEON 为 16 字节，AVX2 为 32 字节。
默认构造为零，显式标量构造广播；`size` 为元素数，lane 0 对应最低地址。
使用 `lane(i)` / `set_lane(i, value)` 访问元素，索引必须小于 size。

- `load<V>` / `store` 允许未按向量宽度对齐的地址；地址仍须满足元素 T 的对齐要求。
- `load_aligned` / `store_aligned` 另要求地址按 `V::alignment` 对齐。
- `load_partial<V>(p, n, fill)` 只读取 n 个元素，其余填 fill；`store_partial` 只写 n 个。
  n 必须不超过 size。n 为零时允许空指针，不做指针运算或内存访问。
- `Mask<T, Abi>` 每个元素一个布尔值，不隐式转换为 bool。使用 `any/all/none/count/first_true`；
  空 mask 的 first_true 返回 nullopt。`first_n<V>(n)` 标记前 n 个有效元素。
- `select(mask, yes, no)` 在已求值的两个 Batch 之间选择，不能保护分支里的非法访问。
- `compress` 返回稳定排列的 values 和 count；尾部清零。
  `compress_store` 只写选中的元素，返回实际数量；空 mask 允许空输出指针。

## 运算契约

| 操作 | 语义 |
| --- | --- |
| 整数 `+ - *`、取负、reduce_sum | 按元素位宽取模，包含有符号类型 |
| `add_saturate/sub_saturate` | 整数饱和到目标类型上下界 |
| `average_round_up` | 无符号平均，奇数和向上舍入，不溢出 |
| `shift_left/shift_right_logical` | 移位量达到位宽时为零 |
| `shift_right_arithmetic` | 仅有符号整数；大移位量填满符号位 |
| 浮点 `+ - * /` | 默认舍入环境；普通乘加分别舍入 |
| `fma(a,b,c)` | 融合乘加，单次舍入；目前逐 lane 调用 std::fma |
| 比较 | NaN 的相等、有序比较为 false，不等为 true |
| `min/max/reduce_min/reduce_max` | 当前仅提供整数版本 |

不启用 fast-math。CMake 的 simd 目标向使用方传递 `-ffp-contract=off`，以维持普通乘加语义；
直接使用头文件时也须设置该选项。FTZ/DAZ 等浮点状态由调用方控制；不规定 NaN payload 和异常标志跨架构一致。
整数除法、FP16 和浮点 min/max 尚未提供。

`interleave(a,b)` 返回 low/high，顺序是完整的 `[a0,b0,a1,b1,...]`；
`deinterleave(a,b)` 从拼接的 `[a,b]` 抽取 even/odd。
AVX2 的顺序跨越整个 256 位，不按两个 128 位半区分别排列。
`shuffle<I...>` 显式指定全部输出索引；`rotate_left<N>` 在完整 Batch 内循环移动。

`widen<To>` 保留所有输入元素，返回 `WideParts` 中按顺序排列的多个 Batch；
整数 widening 保持符号性。`narrow_saturate<To>` 与 `narrow_truncate<To>` 接受完整 parts，
分别饱和或保留低位。`convert_saturate<To>` 为浮点到整数，显式选择四种舍入方式，
NaN 转零，无穷及越界值钳位。`convert<To>` 为整数到浮点；两者要求 lane 数相同。
`bit_cast<To>` 保留全部字节，要求源目标总字节数相同。

原生后端优先提供算术、比较和位运算；x86 字节交织/解交织、8/16 位饱和与平均使用 intrinsic，
NEON 提供原生交织和解交织。缺失指令使用统一标量回退，例如 SSE2 的 64 位比较、
8/64 位整数乘法。相邻整数位宽的扩宽、常用饱和缩窄、16/32 位算术右移，以及
x86 的 16 位交织已有原生路径。AVX2/NEON 的 8/16/32/64 位元素压缩用查表和原生重排，保持稳定顺序及尾部清零。
NEON 的 i32/u32 → float 使用原生转换；SSE2、AVX2、NEON 的 float → i32/u32 饱和转换
支持四种显式舍入方式，不改变调用方的浮点舍入模式。
x86 的 16 位比较先压缩到字节再提取掩码，32/64 位直接提取每个 lane 的符号位；
NEON 的 8/16/32 位比较采用位权归约，64 位直接提取两个 lane。
AVX2 的 32/64 位 `compress_store` 使用精确掩码写出。
NEON 压缩写入使用定长拷贝，64 位部分掩码直接选取唯一元素，仍只写实际选中的元素。
其他转换、归约和通用 shuffle 保留通用实现；具体是否生成向量指令取决于编译器。

## 算法与调度

`mismatch<T, Abi>` 和 `find_if<T, Abi>` 返回首个索引或 nullopt；
`copy_if<T, Abi>` 返回写入数量，保持输入顺序。输出容量至少为输入长度；允许精确原位过滤，
其他重叠不支持。长度为零允许空指针。尾部会叠加有效 lane mask，填充值不参与结果。
默认四参调用及 `FilterWrite::Exact` 保持 `output[count, length)` 原值。
第五参可显式传 `FilterWrite::AllowOverwrite`，允许该范围内容未指定；AVX2/NEON 对完整输入批次可整宽写出，
最后不足一个向量的部分仍精确写入，不会越过 `output[length]`。两种策略都支持精确原位过滤。

```cpp
#include <lutils/simd.hpp>
namespace s = lutils::simd;
using V = s::Batch<std::int32_t, s::abi::sse2>;
auto count = s::copy_if<std::int32_t, s::abi::sse2>(
    input, length, output, [](V v) { return v > V{0}; });
```

这是 SSE2 编译单元中的例子。可运行的跨平台版本见 [examples/simd.cpp](../examples/simd.cpp)。
各 ISA 的外部函数只传指针、长度、stride 等普通参数；不要跨 ISA 边界按值传递 Batch。

运行时创建 `DispatchTable<Signature>`，为已编译实现填写函数指针，调用 `resolve(table, target)`。
Automatic 按 AVX2、NEON、SSE2、Scalar 选择表中存在且 CPU 支持的实现；显式目标不可用时返回 nullopt。
AVX2 还检查 OSXSAVE/XCR0，FMA 单独检测，不视为 AVX2 的隐含能力。
调度在算法入口进行，库不在每条 SIMD 操作中检测 CPU。

ISA 编译选项只加在对应源文件上。不要全项目使用 `-march=native` 或 `-mavx2` 后期待 baseline 回退。
头文件操作强制内联，避免库操作的机器码被跨 ISA 合并；公共模板保留正常的外部链接。
调度表只保存各 ISA 独立命名的 wrapper，不对 SIMD 头文件 API 取地址做调度，
包括算法、基础操作、成员和 hidden-friend。调度表构建和 resolve 放在 baseline 控制编译单元。
用户自己的算法模板也应在对应 ISA 的编译单元实例化，
避免将同一个外部函数实例同时编译为不同指令集后合并。
当前原生构建支持 Linux x86-64 的 GCC/Clang 和 AArch64 的 GCC/Clang（含 Android NDK r28）。

## 图像验证与基准

```cpp
auto result = lutils::image::convertUyvyToNv12(source, destination);
// 可显式传第三参数：simd::Target::Scalar / Sse2 / Avx2 / Neon。
```

source/destination 是 CPU 可访问的 ConstFrameView/FrameView。输入为偶宽 UYVY，输出等尺寸 NV12；
支持奇高、正负 stride、padding 和输出两平面共用分配。源与目标分配必须不重叠。
图像需逐行扫描，水平色度位置已知、输入垂直 Cosited、输出 Midpoint，其他颜色信息保持一致。
Y 原样保留，UV 为相邻两行 `(top + bottom + 1) / 2`，奇数末行复制。
NV12 → RGBA8 使用 BT.601 limited、已知水平色度位置、垂直 Midpoint，输出 Identity/Full；
primaries 和 transfer 不变。色度按 2×2 块复用，Alpha=255，支持奇宽、奇高。
与 compute 内核采用相同的 298/409/100/208/516 定点系数、加 128 后除 256 和饱和规则。
不先截断输入 Y，所有 0–255 输入都与标量参考逐字节一致。

重复处理同一格式时先准备计划；目标 ISA 在 prepare 时确定：

```cpp
namespace image = lutils::image;
auto plan = image::CpuConversionPlan::prepare(sourceDesc, destinationDesc);
if (!plan) return plan.error();
auto result = plan.value().run(sourceView, destinationView);
```

图像层的 Automatic 根据行宽排除无法进入完整向量块的后端：小于 16 像素用 Scalar，
小于 32 像素不选 AVX2；显式 Target 不受此规则限制。通用 `simd::resolve` 保持 ISA 优先级策略。

若要按当前机器、图像尺寸和真实 stride 实测选优，可显式调优一次并复用返回的计划：

```cpp
#include <lutils/image/CpuTuning.hpp>
auto tuned = image::tuneCpuConversion(sourceView, destinationView);
if (!tuned) return tuned.error();
auto result = tuned.value().plan.run(sourceView, destinationView);
```

调优会反复写入目标，调用方须独占源与目标，源内容保持不变。先检查默认后端的预热稳定性，
再用一次探测跳过明显较慢的候选。每个有希望的候选与默认后端交替执行 ABBA/BAAB 配对测量；
胜出者还须通过独立的第二轮确认。两阶段各五个完整块，分别要求至少四次胜出，且
`median(候选/默认) + 2*MAD <= 0.95`；MAD 是比值相对中位数的绝对偏差中位数。
预热不稳定、采样不完整或确认失败均保留默认后端。此规则针对稳态中位吞吐，不判断尾延迟。
`timings` 保存探测值与完整配对均值，各候选样本数可能不同，不能直接当作排名表。
`elapsed` 包含校准和诊断整理成本；`budgetExhausted` 仅表示总预算耗尽，不把阶段上限算作总预算。
默认预算为 100 ms，可用 `CpuTuningOptions` 设置为 (0, 10s]；预算在转换之间检查，
已开始的转换会完成。计时包含视图校验及其元数据分配，不含像素 buffer 分配或映射。
内部以纳秒计算阶段期限，最低预热时间和批次时长随预算缩放，避免小预算被固定 5 ms 门槛挡住。

`outcome` 说明本次控制流程的结果：

| `CpuTuningOutcome` | 含义 |
| --- | --- |
| `Selected` | 候选通过筛选和独立确认 |
| `NoAlternative` | 没有其他可用候选 |
| `NoGain` | 未确认收益，包含冷探测拒绝和确认失败，不表示所有候选都完成了筛选 |
| `WarmupUnstable` | 已达到最低样本数和时长，但稳定性检查未通过 |
| `InsufficientBudget` | 阶段额度结束，必需证据仍不完整，总预算尚未耗尽 |
| `BudgetExhausted` | 总截止时间阻止了必需采样 |

`outcome` 与 `budgetExhausted` 含义独立：完整确认块刚好跨过软截止时间时，
可以同时得到 `Selected` 和 `budgetExhausted=true`。
调优没有全局缓存或后台线程，也不会在普通 `prepare/run` 中自动触发。
数据布局、CPU 调度或负载变化后，调用方可重新调优。

固定硬件的多轮采集、退化判定和 CI 配置见 [性能回归](simd-regression.md)。

对于 `ImageResource`，包含 `<lutils/image/CpuConversionExecutor.hpp>`，链接 `lutils::image_memory`：

```cpp
auto executor = image::createCpuConversionExecutor();
auto result = executor->run(plan.value(), sourceResource, destinationResource);
```

执行器对输入使用 Read、输出使用 Write，每个对象只映射一次；计算后结束两侧 CPU 访问并报告同步错误。
submit 同步完成，返回已 ready 的 Completion；不创建线程，也不复制像素到中间 buffer。
调用方负责在执行期间独占相关资源。设备内存必须支持 CPU 映射，否则返回 Unsupported。

原生 RGBA 输出支持 visible crop 和行 padding，要求 pixelStride=4。原生 YUV/AImage 暴露为
I420 逻辑平面，不能直接作为此处的 NV12 输入。已有 `ConversionPlan::record/run` 仍执行生成的
compute 内核；需要 SIMD CPU 路径时显式使用 `CpuConversionPlan`，不根据设备名称推测后端。

```sh
cmake --preset release -DLUTILS_BUILD_BENCHMARKS=ON
cmake --build --preset release -j2
ctest --preset release -R '^simd\.'
./build/release/examples/simd_example
./build/release/benchmarks/simd/simd_benchmark > build/release/simd.jsonl
uv run --no-project tools/simd/report.py build/release/simd.jsonl --output build/release/simd
```

基准覆盖扩宽、饱和缩窄、整数转浮点、归约、0/50/100% 命中率的稳定过滤，以及两种图像转换。
长度包含 4096、4099、262144；图像包含 640×480、1920×1536 和带 padding 的 642×481。
每项至少预热 50 ms，再记录九组原始样本（迭代次数、总纳秒数、执行前后 CPU 编号），报告中位数、MAD、最小和最大值。
计时前用不同于参考值的数据覆盖输出并独立校验，避免上一后端的结果掩盖漏写；计时后直接校验最后一次执行的结果。
smoke 仅预热一次。
图像分别计时 prepared_run 和 prepare_and_run；分配、参考计算、校验不在计时内。
Scalar 允许编译器自动向量化。

基准固定随机种子，单线程，45 秒总预算。`--smoke` 只验证结果和运行流程，不设跨机器性能阈值。
`tools/simd/report.py` 验证完整性和统计量，导出逐样本和汇总 CSV。
实测环境、优化收益与原始数据见 [SIMD 性能报告](simd-performance.md)。

`simd_resource_benchmark` 测量 VA、Linux/Android DMA、AHB BLOB 和原生 RGBA AHB 输出：

```sh
./build/release/benchmarks/simd/simd_resource_benchmark --memory va
# 仅分配本进程自己的 DMA buffer；需要可访问的 heap。
timeout 50s ./build/release/benchmarks/simd/simd_resource_benchmark --memory dma --heap /dev/dma_heap/system
uv run --no-project tools/android/run.py --serial "$ANDROID_SERIAL" --timeout 50 -- \
    build/android-arm64/benchmarks/simd/simd_resource_benchmark --memory ahb
```

`--memory ahb-rgba` 测量 NV12 BLOB → 原生 RGBA AHB；原生 YUV 输入没有被当作 NV12。
每种存储覆盖平面共用和分离分配。`executor_run` 包含映射、计算、结束 CPU 访问和映射析构；
`mapped_run` 在同一映射内重复计算；`map_finish` 仅测无像素访问的映射/同步/析构。
空映射对照不包含转换引起的脏缓存刷新，三个指标不能按简单相减拆分。
分配、导入、调优、填充和逐像素验证位于计时外；调优耗时另记 `calibration_ns`。
这些私有 CPU buffer 没有在途生产者，不测摄像头、解码器或 GPU fence 的等待。

测试覆盖数值边界、四种舍入模式、NaN、所有尾长、guard page、AVX2 跨半区顺序、混合 ISA 链接、
图像布局与映射同步错误。Android 使用 [设备测试流程](android.md) 运行 `^simd\.`；
`simd.reject` 是可在 PC 运行的 NDK 编译拒绝测试。
