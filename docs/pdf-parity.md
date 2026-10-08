# PDF 功能对齐

依据：Koen Samyn《From Pure ISO C++20 To Compute Shaders》，67 页。
实现使用 C++17，Linux 首发，GPU 后端为 Vulkan。同一份类内核生成 CPU 包装和 SPIR-V。

## 功能与模块

| PDF 页码 | 功能 | 实现与验证 |
| --- | --- | --- |
| 19–29 | 标记结构体、main、fileLocation、local_size | kernelc 类入口；三维工作组和不足一个工作组的尾部 |
| 22–36 | BufferResource、BufferBinding、CPU/GPU 调用 | 类型化资源、attach、直接下标读写；FloatAdder |
| 38–41 | 向量、swizzle、数学函数、结构体、uniform | CameraRays → SphereTracer → VisualizeRays；CPU/GPU 图像差分 |
| 44–52 | D1/D2/D3、imageLoad/imageStore/imageSize | Vulkan 原生 storage image；一维、二维、三维运行测试 |
| 53–62 | Pixel 协议、通道重排/补值、数值和位打包 | C++17 traits、混合通道类型、BGRA、RGB565、RGB10A2 |
| 58–62 | 内外像素格式转换 | 33 种内部格式的 CPU/GPU load/store 往返 |
| 63–66 | 成员数组、循环、布尔转换、ping-pong | Game of Life 四代与独立 blinker 参考 |
| 7–8、25–28 | CMake、Clang、GLSL/SPIR-V 校验 | 公共预处理配置、源码位置诊断、不支持语法的拒绝测试 |

模块分工：

- `Math.hpp`：标量别名、2/3/4 分量向量、读 swizzle、数学函数、调用坐标。
- `Pixel.hpp`：通道协议、数值转换、外部像素预设、内部格式与字节 codec。
- `Shader.hpp`：内核标记、BufferBinding、ImageBinding、Uniform、维度模型。
- `Compute.hpp`：主机资源、std430 codec、类型化 Backend、生成的 KernelTraits 接口。
- `StructCompiler.hpp`：从 Clang AST 提取类型、布局、资源访问和控制流，生成 GLSL 与 CPU 包装。
- runtime / CPU / Vulkan：资源、上传、dispatch、回读、异步完成与资源复用。

原有 UYVY/YUYV → NV12、NV12 → RGBA 也已经使用类内核。类型擦除模块保持独立。

## 写法与构建

源码按公共头文件编写，包含 include guard；普通辅助函数使用 inline，避免重复定义。
实际内核与主机调用分别见 `examples/kernels/pdf.hpp`、`examples/pdf_compute.cpp`。

```cpp
struct LUTILS_KERNEL FloatAdder {
    static constexpr char fileLocation[] = "typed_add";
    uvec3 local_size{256, 1, 1};
    BufferBinding<float, 0> A;
    BufferBinding<float, 1> B;
    BufferBinding<float, 2> C;
    Uniform<float, 0> scale{1.0f};

    void main() {
        uint i = gl_GlobalInvocationID.x;
        C[i] = (A[i] + B[i]) * scale;
    }
};
```

```cmake
lutils_add_shader(adder
    NAME typed_add SOURCE kernels/Adder.hpp ENTRY demo::FloatAdder)
target_link_libraries(app PRIVATE adder lutils::compute_cpu)
# GPU 调用另链接 lutils::compute_vulkan。
```

`fileLocation` 必须等于 CMake 的 NAME，作为生成文件的名字。SPIR-V 嵌入生成库，运行时无需寻找 shader 文件。
`local_size` 从 C++ 声明读取；运行时修改会被拒绝。旧的普通函数接口使用 `lutils_add_kernel`，两种入口误用会报错。

类内核的预处理配置属于公共接口。`lutils_add_shader` 将宏、宏选项和 include 路径同时传给
Clang、CPU 包装和调用方，包括目录、目标及依赖带来的配置。调用方不能覆盖这些宏；
生成头检查已记录宏的定义状态和直接配置值的一致性；它不检测所有间接宏带来的类型变化。
语义宏必须通过构建配置声明，不使用额外的调用方宏、
编译器内建宏或平台内建宏改变内核定义。命令行宏只支持对象式宏。

```sh
./build/compute/examples/pdf_compute cpu
./build/compute/examples/pdf_compute gpu output.ppm
```

示例依次运行加法、三阶段光线追踪和 Game of Life；可选路径保存光线追踪的 PPM 图像。

## 资源与执行契约

`BufferResource<T, Dim>` 拥有真实 T 对象；默认 D1，图像也支持 D2/D3。
`attach` 借用资源，借用必须持续到命令录制结束。移动资源后需要把绑定重新 attach 到新对象。
`Backend` 拥有设备和资源的设备副本；失效主机资源对应的副本会在后续上传时清理，
已录制/在途命令仍保留自己的句柄。上传失败不会发布可用副本。

上传后，通过绑定的 `operator[]`、`imageLoad`、`imageStore` 编写算法。Backend 提供
`uploadBuffer`、`uploadImage<Format>`、`downloadBuffer`、`downloadImage<Format>`、
`useKernel`、`bindBuffer`、`bindImage`、`bindUniform`、`execute` 和 `record`。
`execute` 返回 Completion；`record` 允许把多个阶段放入同一 CommandList，录制时保存 uniform 和资源句柄快照。
`cpu::CPUBackend` / `gpu::GPUBackend` 是默认设备的便捷入口；配置 VulkanOptions 时可显式构造 Backend。

Buffer、Image、Uniform 各自拥有独立的逻辑 slot 空间，允许跨种类复用编号。
生成器把资源按声明顺序映射为不同的 Vulkan descriptor binding，Uniform 映射为 push constant 偏移。
同种资源的重复 slot 被拒绝；同次 dispatch 的可写绑定不能别名。

execute 的范围是工作项数量。Vulkan 向上取工作组数，生成的入口先检查实际范围；CPU 只执行实际范围。
工作组坐标和数量在两端一致。图像算法仍需自行处理邻域边界；GPU 不提供 CPU 越界异常。
同一 Device 及其 Completion 的操作由调用方串行化。

## 类型、布局与像素

支持 bool、int32、uint32、float，及设备支持时的 double；向量有 2/3/4 个分量。
读 swizzle 使用 `v["xy"_sw]`，用目标向量接收；单分量可转换为标量。分量数不匹配会报错。
支持 dot、cross、length、normalize、reflect、min/max/clamp、sqrt、abs、pow、floor/ceil、sin/cos。

Buffer 可存标量、向量和无继承的平凡聚合结构体，结构体可嵌套并包含非空 std::array。
生成的 std430 codec 逐字段打包，正确处理 vec3、数组 stride 和结构体 padding。
CPU 包装每次 dispatch 解码一次，完成后写回；Word buffer 可直接使用已有 Word 对象。
不把 uint32 存储强制转换为 float 对象。

Uniform 支持标量、向量和这些值结构体；push constant 总计最多 128 字节，其中 16 字节保存执行范围。
kernel 是无用户构造/析构和继承的聚合。成员数组必须有常量初始化且在 kernel 中只读；
调用方修改数组后，录制会拒绝与编译值不同的实例。

Pixel 协议是 `ChannelType<C>`、`get<C>()`、`set<C>()`，允许各通道使用不同类型。
C++17 detection traits 代替 Concepts。预设覆盖 R/RG/RGB/BGR/RGBA/BGRA 的
8/16 位整数、UNorm、SNorm，以及 32 位浮点。内部 storage image 覆盖 R/RG/RGBA：
8/16 位 UNorm、SNorm、UInt、SInt，32 位 UInt、SInt、Float，共 33 种。

通道按语义重排，缺失通道补 `(0,0,0,1)`。整数外部通道转浮点或 normalized 内部格式时归一化；
反向转换按外部通道范围量化，可通过 ChannelConverter 定制。显式整数内部格式使用数值饱和转换。
浮点图像传输保留 Inf、NaN 和有符号零；浮点计算不承诺跨设备位级相同。
CPU normalized 编码使用最近整数舍入，原生 GPU imageStore 的边界舍入可能相差一个量化单位；
光线追踪差分据此允许每通道 1/255。整数图像和 YUV 整数算法要求逐字节相同。

PackedPixel 的通道位从最低位按 R、G、B、A 排列，RGB565 使用 16 位容器，RGB10A2 使用 32 位容器；
交换文件或设备数据时应核对对方位序。自定义 Pixel 可以描述不同位序。
图像创建检查维度、整数范围、格式与设备能力；不支持的设备格式返回错误。

## 边界

Clang 前端支持示例用到的局部值、分支、for/while、普通辅助函数和显式数值转换。
明确拒绝指针/引用值、动态分配、递归、虚调用、任意 STL、零长/嵌套数组、64 位整数运算，
以及不能保持 C++ 求值顺序的嵌套修改。用户自定义运算符不被当作 GLSL 内建运算符替换。
每个工作项必须独占写入位置；目前没有共享内存、原子或组内屏障。

第 44 页 Cube/Array 等是 GLSL API 背景；第 67 页 float16、dimensional spans 和 reflection
是作者的未来工作。本次对齐已展示的功能，不包含这些扩展。Windows 尚未验证。

## 验证记录

2026-10-08，在 Linux、GCC 13.3 / Clang 18、RTX 5070 Ti（驱动 595.91.07）上完成复验：

- 默认构建：12/12；包含 kernelc 和 Vulkan 的 Release：47/47。
- Clang ASan / UBSan / LeakSanitizer：47/47，Vulkan 测试显式使用 Lavapipe，泄漏检测保持开启。
- RTX 5070 Ti：类内核、33 种内部图像格式、嵌套结构体布局、D1/D2/D3、光线追踪与
  Game of Life 全部通过；Vulkan 验证层 0 errors / 0 warnings。
- 原有图像转换链的显存与时间戳路径通过，同步验证 0 errors / 0 warnings。
- 用户提供的 30 帧 1920×1536 UYVY 全部转换成功；NV12 与独立参考逐字节一致。
- CPU 示例通过，输出 FloatAdder、129×97 光线追踪及四代 Game of Life。

本机 NVIDIA 路径在程序退出时仍有 `libdbus-1.so.3` 的 LSan 泄漏报告。
一个仅创建/销毁 Vulkan instance 和 device、未链接本项目的独立程序也复现了
1981 字节、3 次分配的泄漏。这证明该报告可以在外部 Vulkan 依赖栈独立出现，
尚未区分驱动与其依赖的具体责任；因此不将 NVIDIA 路径记录为 LSan 全通过。

复跑完整测试的命令：

```sh
ctest --test-dir build/debug --output-on-failure
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ctest --test-dir build/compute-release --output-on-failure
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build/compute-sanitize --output-on-failure
```

UYVY → NV12 短测使用上述 30 帧，预热一轮、测量三轮，共 90 帧，流水线深度 2。
Release GPU 显存路径为 565.89 帧/秒，延迟中位数 2.964 ms、P95 5.604 ms；
串行 CPU 参考后端为 104.40 帧/秒。计时包含打包、上传、计算、回读、解包和流水线填充/排空，
不含文件 I/O、初始化和参考校验；未绑核、未锁频，性能测试关闭验证层及时间戳。
这次短测与此前十轮历史记录的运行条件不同，不能据此判断性能提升或退化。

本地日志、基准 JSON、NV12 输出和独立泄漏探针保存在忽略目录
`build/pdf-parity/verification-20261008/`，其中 `release-tests.log`、`sanitize-tests.log`、
`shader-hardware.log`、`conversion-hardware.log` 和 `uyvy-hardware.log` 保存上述验收结果。
