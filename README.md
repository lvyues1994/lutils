# lutils

C++17 工具库，提供类型擦除、图像模型及 CPU/Vulkan 计算模块。类型擦除模块为无第三方依赖的头文件库。

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

另有 `release`、`clang-sanitize` 预设，分别验证优化构建和 AddressSanitizer／UBSan。若运行环境不支持 LeakSanitizer，可用 `ASAN_OPTIONS=detect_leaks=0 ctest --preset clang-sanitize` 单独关闭泄漏检测。

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
| `lutils::image` | 通用固定块格式描述、颜色信息、正负 stride 视图、主机帧 |
| `lutils::compute_kernel` | C++17 内核资源访问类型 |
| `lutils::compute`、`lutils::compute_cpu` | 任务、资源、CPU 参考执行 |
| `lutils::compute_vulkan` | 可选 Vulkan 1.1 计算后端 |
| `lutils::image_ops` | 由 C++ 内核生成的图像转换及设备帧 |

基础图像和 CPU 运行时随默认构建启用。构建内核工具与图像算法需要匹配的
Clang/LLVM 18 开发文件、`glslangValidator`、`spirv-val`；Vulkan 后端另需 Vulkan
开发文件、loader 和可用设备驱动。Ubuntu 对应包为 `libclang-18-dev`、`llvm-18-dev`、
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

RTX 5070 Ti 的当前实测：单帧中位延迟 1.665 ms；两帧流水线完整吞吐 660 帧/秒，
其中包括主机打包、传输和解包。测量边界与两种配置的延迟取舍见
[流水线报告](docs/gpu-pipeline-benchmark.md)；[前一阶段报告](docs/gpu-benchmark.md)保留历史基线。

### 编写内核

内核是普通 C++17 函数，入口使用 `Invocation`、一个或多个只读/只写 word buffer，
以及扁平的 32 位参数结构。示例见 [add_kernel.cpp](tests/compute/add_kernel.cpp)。

```cmake
lutils_add_kernel(my_kernel
    NAME my_add
    SOURCE kernels/add.cpp
    ENTRY my_app::add
    LOCAL_SIZE_X 32
    INCLUDE_DIRS include
    DEFINITIONS MY_KERNEL_OPTION=1)
target_link_libraries(my_app PRIVATE my_kernel lutils::compute_cpu)
```

生成 `my_add.hpp`、独立 CPU 包装、GLSL、SPIR-V；头文件提供
`lutils::generated::my_add()` 和 `pack_my_add(my_addParams const&)`。
CPU 包装独立编译，消费目标的宏不会重新解释算法。影响算法语义的宏请通过
`DEFINITIONS` 传入；目录/目标定义、配置宏（如 `NDEBUG`）和常规 `-D`/`-U` 同步到 Shader 编译。
使用 Ninja 等单配置生成器；语言条件宏表达式、强制包含等不支持的设置会报错。
内核不能根据编译器或平台内建宏改变算法行为。
被多个内核入口共享的源文件中，函数定义使用 `inline` 或内部链接。
工具会追踪传递头文件依赖，同时更新 CPU 包装和 Shader。

当前支持标量运算、普通标量辅助函数、分支、for/while 循环；参数最多 128 字节，
工作组 X 通过 `LOCAL_SIZE_X` 指定，缺省为 1，Y/Z 固定为 1。图像算子默认 X=32，
可用 `LUTILS_IMAGE_LOCAL_SIZE_X` 调整。dispatch extent 表示实际工作项数量，必须整除
工作组大小；图像层向上补齐 X，内核检查真实尺寸并跳过尾部。
资源以 32 位 word 访问，写入者必须独占整个 word。
未知语法、递归、动态分配、嵌套修改表达式等在生成阶段拒绝。

Vulkan 已支持 host-visible/device-local storage、staging、执行槽复用和单队列异步提交；
已验证 CPU、软件 Vulkan 和 RTX 5070 Ti。Windows、其他硬件、多队列传输重叠、
向量/数组内核、共享内存和原子操作仍待扩展。
