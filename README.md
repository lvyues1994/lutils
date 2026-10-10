# lutils

C++17 工具库，提供 expected、类型擦除、图像模型及 CPU/Vulkan 计算模块。
expected 和类型擦除均为无第三方依赖的头文件库。

## expected

`#include <lutils/Expected.hpp>`，链接 `lutils::core`。
`lutils::expected<T, E>` 使用与标准库一致的接口命名，支持 `void`、移动专用类型及链式操作：

```cpp
lutils::expected<int, std::string> parseNumber(std::string_view text);

auto result = parseNumber("42")
    .transform([](int n) { return n * 2; });
if (result)
    use(*result);
else
    report(result.error());
```

实现依据、异常保证和 C++17 限制见 [expected 设计与用法](docs/expected.md)。
可运行示例为 [examples/expected.cpp](examples/expected.cpp)。

## 类型擦除

定义一个接口模板，就可以让拥有对象和借用视图直接调用成员函数。具体类型无需继承接口。

```cpp
#include <lutils/erasure.hpp>

namespace te = lutils::erasure;

template <class Model>
struct ICounter : te::Interface<ICounter, Model, te::Extends<te::ICopyable>> {
    using ICounter::Interface::Interface;

    virtual int value() const { return te::value(*this).value(); }
    virtual void setValue(int n) { te::value(*this).setValue(n); }
};

struct Counter {
    int n = 0;
    int value() const { return n; }
    void setValue(int value) { n = value; }
};

auto counter = te::Any<ICounter>{Counter{10}};
auto copy = counter; // 按 Counter 的复制构造函数复制目标
auto borrowed = te::AnyPtr<ICounter>{&counter};
borrowed->setValue(20);
auto readOnly = te::AnyConstPtr<ICounter>{borrowed};
auto result = readOnly->value(); // 20，copy.value() 仍为 10
```

完整可运行示例见 [examples/counter.cpp](examples/counter.cpp)。

### 常用操作

| 操作 | 用法 |
| --- | --- |
| 原位构造 | `te::Any<ICounter>{std::in_place_type<Counter>, ...}` |
| 借用普通对象 | `te::AnyPtr<ICounter>{&object}` |
| 借用 const 对象 | `te::AnyConstPtr<ICounter>{&constObject}` |
| 判断空状态 | `te::empty(owner)`；视图也支持 `if (pointer)` |
| 释放目标／解除借用 | `te::reset(owner)` / `te::reset(pointer)` |
| 替换目标 | `te::emplace<Counter>(owner, ...)`，返回新目标的引用 |
| 交换 | `te::swap(a, b)` |
| 启用 SBO | `te::Any<ICounter, te::SmallBufferStorage<64>>` |

接口用 `Extends<IBase, ...>` 组合；借用视图可显式构造为基接口视图。接口仅包含行为，继承库提供的构造函数，不添加实例字段、自定义构造函数或特殊成员函数。`_te_` 前缀和 `detail` 命名空间属于内部实现。

`ICopyable` 允许复制和移动，`IMovable` 只允许移动；都不声明时，拥有对象只能原位构造和替换。视图始终可以复制，复制的是借用关系。详细的生命周期、异常和存储约定见 [模块设计](docs/erasure-design.md)。

## 构建与测试

需要 CMake 3.21 或更新版本；预设使用 Ninja。

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/examples/erasure_counter
```

预设分工与安装方式见 [SDK 接入](docs/sdk.md)。`clang-sanitize` / `compute-sanitize` 使用 ASan、UBSan，
`cpu-tsan` 检查线程池与工作组同步。sanitizer 构建用于验证，安装包使用 `sdk-release`。

作为 CMake 子项目使用：

```cmake
add_subdirectory(path/to/lutils)
target_link_libraries(your_target PRIVATE lutils::erasure)
```

嵌入其他项目时，测试和示例默认关闭；启用时，编译告警和 sanitizer 选项用于本仓库目标，不修改父项目的全局选项。

## 图像与计算

架构和执行契约见 [计算模块设计](docs/compute-design.md)，类内核 API 与功能验收见
[PDF 功能对齐](docs/pdf-parity.md)。内核使用 `struct + BufferBinding/ImageBinding/Uniform + main()`，
C++17 源码经 Clang 生成 GLSL/SPIR-V，并保留同一源码的 CPU 参考执行。

| 目标 | 内容 |
| --- | --- |
| `lutils::image` | 通用固定块格式描述、颜色信息、正负 stride 视图、主机帧、CPU 裁剪与填充 |
| `lutils::image_memory` | 统一图像资源、CPU 映射、Linux dma-buf 和 Android AHB BLOB 接入 |
| `lutils::compute_kernel` | C++17 内核资源访问类型 |
| `lutils::compute`、`lutils::compute_cpu` | 任务、资源、CPU 参考执行 |
| `lutils::compute_vulkan` | 可选 Vulkan 1.1 计算后端 |
| `lutils::image_ops` | 由 C++ 内核生成的图像转换、设备帧与裁剪/填充执行器 |

裁剪、背景填充和直接访问共享内存的用法见 [图像区域操作](docs/image-regions.md)，
可运行示例为 [examples/image_regions.cpp](examples/image_regions.cpp)。
NDK r28 交叉编译、Android 设备测试与共享内存接口见 [Android 支持](docs/android.md)。

基础图像和 CPU 运行时随默认构建启用。构建内核工具与图像算法需要匹配的
Clang/LLVM 18 开发文件、`glslangValidator`、`spirv-val`；Vulkan 后端另需 Vulkan
开发文件、loader 和可用设备驱动。Ubuntu 对应包为 `libclang-18-dev`、`libclang-cpp18-dev`、`llvm-18-dev`、
`glslang-tools`、`spirv-tools`、`libvulkan-dev`。

```sh
cmake -S . -B build/compute -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLUTILS_BUILD_KERNELC=ON -DLUTILS_ENABLE_VULKAN=ON
cmake --build build/compute
ctest --test-dir build/compute --output-on-failure
./build/compute/examples/image_compute cpu 642 481
./build/compute/examples/image_compute vulkan 642 481
./build/compute/examples/pdf_compute cpu
./build/compute/examples/pdf_compute gpu output.ppm
```

非标准依赖路径可通过 `LLVM_DIR`、`LUTILS_CLANG_INCLUDE_DIR`、
`LUTILS_CLANG_CPP_LIBRARY`、`LUTILS_GLSLANG`、`LUTILS_SPIRV_VAL` 指定。
第三方依赖不会在普通 CMake 配置时自动下载。

[完整示例](examples/image_compute.cpp) 先上传 YUYV422，在设备上连续执行
NV12 和 RGBA8 转换，再下载最终结果。当前转换支持逐行扫描：

- YUYV422 / UYVY422 → NV12：偶宽，允许奇高，UV 垂直两行平均并按最近整数舍入，末行复制。输入水平位置已知、垂直位置 cosited，输出垂直位置 midpoint。
- NV12 → RGBA8：BT.601 limited，输入水平位置已知、垂直位置 midpoint；色度按所在 2×2 块重建，输出 full-range RGB、alpha 255，输出色度位置设为 Unknown。
- 色域与传递函数保留；算子所需信息未知或请求不受支持时返回错误。

### 类内核示例

[内核源码](examples/kernels/pdf.hpp) 包含浮点加法、三阶段球体光线追踪、Game of Life，
以及一维和三维图像算子。[主机程序](examples/pdf_compute.cpp) 展示上传、attach、execute、
多个阶段录入同一 CommandList、回读及 ping-pong。
用 `lutils_add_shader(target NAME ... SOURCE ... ENTRY ...)` 构建类内核；`fileLocation` 与 NAME 一致，
工作组大小在内核的 `local_size` 中声明。绑定直接使用 `A[i]`、`imageLoad`、`imageStore`。

### UYVY raw 文件测试

启用测试与内核编译器后，`uyvy_nv12_test` 可批量转换紧密排列的 UYVY 文件，
逐字节对比独立参考结果，通过后保存 NV12（完整 Y 平面后接交错 UV 平面）：

```sh
./build/compute/tests/uyvy_nv12_test cpu 1920 1536 output/cpu input/*.uyvy
./build/compute/tests/uyvy_nv12_test vulkan 1920 1536 output/vulkan input/*.uyvy
```

输入字节顺序为 U、Y0、V、Y1，参照 [V4L2 UYVY 定义](https://docs.kernel.org/userspace-api/media/v4l/pixfmt-packed-yuv.html)。
测试按逐行扫描处理；Y 保留，UV 相邻两行按 `(top + bottom + 1) / 2` 平均，奇高复制末行。
1920×1536 输入应为 5,898,240 字节，输出为 4,423,680 字节。测试器拒绝覆盖已有输出。

### 有序传输与异步收取

一次提交可以完成上传、一个或多个算子和回读。以下 `take/check` 为示例中的 Result 检查辅助函数：

```cpp
co::CommandList commands;
check(im::recordUpload(commands, im::readOnly(input.view()), source));
check(plan.record(commands, source, target));
auto readback = take(im::recordReadback(commands, target));
auto done = take(device->submit(commands));
// 可以继续准备、提交其他帧。
check(readback.copyTo(*done, output.view()));
```

recordUpload 拥有打包数据；FrameReadback 不保存主机指针，在 copyTo 时才借用输出内存。
同一 Device 及其所有 Completion 调用需要统一串行化。多帧流水线为每个槽准备独立
DeviceFrame，收取旧结果后再复用该槽。完整示例见 [image_compute.cpp](examples/image_compute.cpp)，
连续帧实现见 [runPipeline](benchmarks/uyvy_nv12.cpp)。

### 硬件验证与性能基准

```sh
cmake --preset compute-release
cmake --build --preset compute-release
ctest --preset compute-release
./build/compute-release/tests/compute_tests vulkan --require-hardware --device-local --validation --timestamps
./build/compute-release/tests/compute_async_tests vulkan --require-hardware --validation --timestamps
./build/compute-release/benchmarks/uyvy_nv12_benchmark vulkan 1920 1536 gpu.json \
  --require-hardware --device-local --pipeline-depth 2 --rounds 10 input/*.uyvy
./build/compute-release/benchmarks/uyvy_nv12_benchmark cpu 1920 1536 cpu.json \
  --pipeline-depth 2 --rounds 10 input/*.uyvy
```

验证命令需要 `VK_LAYER_KHRONOS_validation`；非系统安装可配置 `VK_LAYER_PATH` 和
`LD_LIBRARY_PATH`。正式性能命令关闭验证层与时间戳；诊断时可加 `--validation --timestamps`。
`--require-hardware` 只接受独立或集成 GPU。默认预热一轮、测量三轮，报告拒绝覆盖已有文件。
流水线 JSON 保存整轮耗时、逐帧延迟、全部原始样本、资源创建计数；每轮计时后逐字节验算。
不传 `--pipeline-depth` 时运行单帧分阶段测试和驻留批次测试，`--batch N` 控制驻留批次大小。

`--device-local` 对应 `VulkanOptions::deviceLocal`：使用设备本地 storage 和独立 staging，
默认关闭。提交资源默认复用，`--no-reuse` 可作对照；API 用 maxInFlight 限制在途提交，
用 maxCachedStagingBytes 限制空闲 staging 缓存。`--host-cached` 对应 host-visible storage
的缓存偏好，收益依赖硬件和访问方式。

当前优化与对照数据见 [性能报告](docs/compute-performance.md)。历史阶段数据保留在
[流水线报告](docs/gpu-pipeline-benchmark.md) 和 [初始基准](docs/gpu-benchmark.md)。

### 编写内核与计算能力

新内核使用上述类接口和 `lutils_add_shader`。支持标量、向量、结构体、数组、Uniform、
D1/D2/D3 原生图像，以及 `SharedArray`、`barrier()`、32 位原子操作和 FP16。
完整约定及示例见 [计算能力](docs/compute-capabilities.md)。普通类内核允许尾部工作项；
使用共享内存或屏障时必须提交完整工作组。

原有函数入口 `lutils_add_kernel` 继续支持 `Invocation`、word buffer 和扁平参数，
X 工作组由 `LOCAL_SIZE_X` 指定，Y/Z=1，dispatch 范围必须整除工作组。
[旧入口示例](tests/compute/add_kernel.cpp) 用于兼容性验证；它不提供类接口的类型化资源和组内同步。

Linux CI 包含基础 GCC/Clang、完整计算、sanitizer、下游工程、安装迁移和 TGZ 接入测试。
安装包按需提供 `vulkan`、`image_ops`、`kernelc` 组件，步骤见 [SDK 接入](docs/sdk.md)。
Windows、Cube/Array image 和多队列传输尚未提供。
