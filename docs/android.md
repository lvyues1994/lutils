# Android / NDK r28

使用 C++17 与 NDK r28c（28.2.13676358）。预设目标为 arm64-v8a、API 29，测试程序静态链接 libc++。
基础 image、CPU、Vulkan 和 GLES compute 均可在 Android 编译；kernelc、glslang 和 spirv-val 在 PC 上运行。
GLES 的独立预设、扩展条件和外部内存接入见 [OpenGL 后端](opengl.md)。
kernelc 解析时使用 NDK target、sysroot 和 libc++ 头文件，因此 Android 条件编译与目标代码一致。

## 构建与设备测试

PC 的 LLVM/Clang 18、glslang、spirv-tools 依赖见 [SDK 构建说明](sdk.md)。

```sh
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
cmake --preset host-kernel-tools -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build --preset host-kernel-tools --target lutils-kernelc

cmake --preset android-arm64
cmake --build --preset android-arm64
cmake --preset android-compute
cmake --build --preset android-compute
```

`LUTILS_HOST_KERNELC` 可指定另一个本地主机编译器路径。`LUTILS_BUILD_IMAGE_OPS` 控制生成算子，
`LUTILS_BUILD_KERNELC` 只控制是否构建本机工具；交叉编译时后者必须关闭。

设置 `ANDROID_SERIAL` 为 `adb devices` 中选定的设备序列号，再启用测试：

```sh
cmake --preset android-compute -DLUTILS_ANDROID_DEVICE_TESTS=ON \
  -DLUTILS_ANDROID_SERIAL="$ANDROID_SERIAL"
ctest --preset android-compute --output-on-failure
```

运行器需要 PC 上的 adb、uv 和 Python 3，使用已安装的 Python，不联网安装依赖。
每项运行测试推送到独立的 `/data/local/tmp/lutils-*` 目录，完成后清理并保留退出码。
编译拒绝测试使用 NDK；kernelc 自身测试在 PC 运行。`downstream.android` 安装并移动 SDK，
分别编译预编译算子消费者和使用主机 kernelc 的消费者；启用设备测试时再到设备运行。

## AHardwareBuffer 线性共享内存

`AndroidHardwareBufferMemory` 适配 `AHARDWAREBUFFER_FORMAT_BLOB`，宽度为容量字节数，
height/layers 必须是 1，usage 需包含 `GPU_DATA_BUFFER`。CPU 映射还要求相应 CPU usage。
可靠的别名识别使用 API 31 的稳定 buffer ID；API 26–30 可以运行普通 compute，AHB 适配返回
Unsupported。ID 查询动态加载，不使普通运行时依赖 API 31 的链接符号。

```cpp
#include <lutils/compute/AndroidHardwareBuffer.hpp>
#include <lutils/image/DeviceRegion.hpp>
namespace co = lutils::compute;
namespace im = lutils::image;

auto memory = co::AndroidHardwareBufferMemory::allocate(byteCapacity);
// 检查 Result；planes 描述已知线性布局中的 object、offset 和字节 stride。
auto image = im::ImageResource::linear(description, {memory.value()}, planes);
auto bound = im::bindImage(device, image.value());
// bound 可交给 createDeviceRegionExecutor(device) 重复提交。
```

导入外部 BLOB 使用 `AndroidHardwareBufferMemory::import(buffer)`，内部 acquire 引用；原始引用仍由
调用者管理。多个平面可以共用一个 BLOB，也可以分别使用不同 BLOB。现有五种预设格式与 region plan
均可使用此布局。GPU 使用原分配，执行器不创建 staging buffer、不上传或回读整帧。

普通 VA 使用 CPU 路径。Android 也可以通过 `DmaBufMemory::import(fd)` 导入裸 DMA fd，
再用 `ImageResource::linear` 显式提供格式、平面 offset 和 stride，交给 CPU 执行器。
调用方必须已知线性布局，buffer 必须支持 CPU 映射和 dma-buf 同步 ioctl。
`importDmaBufImage` 便利接口仍仅用于 Linux；这里没有增加 Android Vulkan DMA 导入。

## 原生 RGBA/YUV 图像

`image_memory` 提供 `importAndroidImage(AHardwareBuffer*, options)` 和
`adoptAndroidImage(AImageOwner, options)`，头文件为 `<lutils/image/AndroidImage.hpp>`。
支持单层、非 protected 的 RGBA8 和 8-bit YUV420；同样要求 API 31 的稳定 buffer ID。

原生图像通过 `NativeImage` 表达，不提供虚构的线性 offset。CPU 使用 `lockPlanes` 的实际
row stride、pixel stride 和 Y/Cb/Cr 顺序。YUV 在 `FrameDesc` 中采用逻辑 `i420()` 三平面；
这不表示 AHB 的物理存储为连续 I420，也不预设它是 NV12。YUV 尺寸和裁剪坐标必须偶数对齐。
色彩信息由 `options.color` 显式提供；默认保留 Unknown。

| 区域操作 | CPU | Vulkan GPU |
|---|---|---|
| 原生 RGBA → 原生 RGBA | 支持 | 直接 image copy，填充使用 image clear |
| 原生 RGBA → 线性 RGBA | 支持 | nearest 采样写入 storage buffer |
| 原生 YUV420 → 线性 I420 | 支持 | 原始 Y/Cb/Cr 采样，无矩阵或范围转换 |
| 原生 YUV420 输出 | 支持 | 返回 Unsupported |
| 线性输入 → 原生输出 | 支持 | 返回 Unsupported |

GPU 线性输出可以使用 AHB BLOB 或设备 buffer。原生输入要求合适的 GPU usage 和设备外部图像能力；
CPU 映射要求对应的 CPU usage。填充原生 RGBA 时，GPU 目标必须覆盖完整 AHB。
GPU 路径不创建整帧 staging；不支持的组合返回错误。

```cpp
auto image = im::importAndroidImage(hardwareBuffer, {{}, producerLease, acquireFence});
// 检查 Result。区域以可见图像为坐标系，output 为调用者提供的兼容资源。
auto plan = im::RegionPlan::crop(image.value().description(), {x, y, width, height});
auto executor = im::createDeviceRegionExecutor(device); // CPU 用 createCpuRegionExecutor()
auto done = executor.value()->submit(plan.value(), image.value(), output);
```

`importAndroidImage` acquire 一个 AHB 引用，原始引用仍由调用者管理。若生产者可能重用存储，
应通过 `owner` 保留对应帧租约，直至映射和 GPU 使用结束。

### AImage 的接管

```cpp
// reader 是拥有 AImageReader 的 shared_ptr，frame/fd 来自 acquireNextImageAsync。
co::FileDescriptor acquire{fd};
auto image = im::adoptAndroidImage(im::AImageOwner{frame}, {{}, reader, acquire.get()});
```

接管会消费 `AImageOwner`，失败时也释放帧。`owner` 必须实际保有该帧的 AImageReader，
不要再手动 delete image 或 reader。返回资源只读，自动采用 AImage crop rect；
CPU mapping 和 GPU Completion 保留租约，完成后才允许 BufferQueue 回收帧。
接管前不要调用 `AImage_getPlaneData/RowStride/PixelStride`：这些接口会保留 AImage 的 CPU lock，
并且没有独立 unlock。需要 CPU 访问时使用返回资源的 `nativeStorage()->map()`。

GPU 原生采样也可用于自定义 C++ 算子：`kernel::SamplerBinding<slot>` 和
`kernel::sampleNearest(binding, uv)` 生成 `sampler2D`，采用归一化坐标、nearest、clamp-to-edge。
RGBA 返回 RGBA；YUV 返回 `(Cr/255, Y/255, Cb/255, 1)`，使用 `RGB_IDENTITY` 保留原始码值。
运行时按外部格式缓存 immutable sampler 和 pipeline，缓存不持有 AImage 帧。

三种 Binding 都可直接 attach 设备句柄。例如 `backend` 持有 Vulkan Device，`shader.source`
为 `SamplerBinding`，`shader.destination` 为对应元素类型的 `BufferBinding`：

```cpp
auto input = backend.device().importImage(image.value().nativeStorage());
auto target = backend.device().importMemory(outputMemory); // AHB BLOB
// 检查 Result；其他参数和绑定按算法设置。
shader.source.attach(input.value());
shader.destination.attach(target.value());
auto done = backend.execute(shader, {width, height, 1});
```

此路径直接使用导入资源，无隐式 upload/readback。binding、已录制命令和在途提交都保留资源引用；
调用方不再使用某帧时应解除绑定并释放自己的句柄。传给自定义采样算子的坐标对应完整 AHB allocation，
AImage 的可见裁剪区域需由算子应用 `visibleRectangle()`。原生图像的 storage 写入能力没有增加，
不支持的 `ImageBinding` 操作仍会被后端拒绝。

## 同步与生命周期

外部生产者的 acquire fence 通过 `importSyncFile(fd, access)` 借入并复制；库合并已有依赖。
`exportSyncFile(access)` 返回拥有的 fd，`-1` 表示当前没有待完成依赖。失败的 fence 返回错误。
CPU map 等待 fence 后 lock；finish 使用同步 unlock 并报告错误。GPU 提交等待输入/输出依赖，
完成 fence 发布到全部资源，包括只读输入；CPU map 可直接等待结果而不先调用 Completion::wait。

同 ID 的活跃 Memory/Mapping 共享 fence 状态。调用者串行化同一分配的访问，在外部异步操作期间保留
Memory；销毁最后一个适配对象后重新导入，必须重新提供生产者 fence。引用保活不代替摄像头、解码器等
生产者的帧访问权协议。提交后 fence 发布失败沿用 [区域操作的错误语义](image-regions.md#同步与访问权)。

## 安装

```sh
cmake --install build/android-compute --prefix build/android-sdk
cpack --config build/android-compute/CPackConfig.cmake -B build/packages
```

下游用相同 NDK、ABI 和 API，传 `-Dlutils_DIR=/path/to/android-sdk/lib/cmake/lutils`，
调用 `find_package(lutils CONFIG REQUIRED COMPONENTS image_ops vulkan)`。
需要生成新算子时，额外指定 `LUTILS_HOST_KERNELC` 并请求 `kernelc` 组件。Android 包不包含 PC 编译器。
静态库最终链接到 APK 的多个 JNI `.so` 时，应由应用统一选择 `c++_shared`，不要在不同 `.so` 间混用
多份 libc++ 状态，见 [NDK C++ runtime 说明](https://developer.android.com/ndk/guides/cpp-support)。
NDK r28 默认生成支持 16 KB 页的 ELF，见 [Android 页大小支持说明](https://developer.android.com/guide/practices/page-sizes)；
实际设备页大小仍须单独验证。

## 本次验证

2026-10-11，NDK r28c、arm64-v8a、编译 API 29；设备为 Android 16 / API 36、Adreno 830：

- Android CTest 共 86 项：85 项通过，1 项 Vulkan FP16 测试因设备能力不足跳过。
  运行测试通过 adb 在设备执行，编译拒绝与 kernelc 测试在 PC 执行。
- 不启用 Vulkan 和生成算子的 `android-arm64` 基础预设另有 35 项测试，全部通过。
- 普通 CPU/Vulkan 转换、裁剪、填充及现有 shader 测试通过。设备缺少 Float64，已验证内核创建返回
  Unsupported，然后继续执行其余 shader 测试。
- AHB BLOB 的 RGBA8、UYVY、YUYV、NV12、I420 裁剪与背景填充通过 CPU/Vulkan 对照；
  包括单块/多块分配、非紧密 stride、保护字节、IPC 句柄别名、连续提交和资源保活。
  GPU 路径的 staging/readback buffer 创建计数均为零。
- 同步测试包含 CPU 映射等待 GPU fence；失败 fence 另用 ioctl/poll 故障注入验证错误传播。
- 原生 RGBA/YUV CPU 操作、RGBA Vulkan copy/clear、RGBA/YUV nearest 采样到线性输出通过；
  采样覆盖 0–255 码值、裁剪偏移、非对齐输出和保护字节，staging/readback 创建计数为零。
- 私有 BufferQueue 产生的 RGBA/YUV AImage 通过接管、只读保护和生命周期测试，包括先释放调用方
  resource/reader、由 mapping 或 GPU Completion 保留帧。未使用摄像头；MediaCodec 解码路径未验证。
- 安装并移动 SDK 后，下游预编译算子和新生成 shader 均能交叉编译并在设备运行；
  原生图像和 AImage 接口也完成安装包链接检查。
- Linux CTest 共 86 项：84 项通过，2 项真实 dma-buf 测试在沙箱跳过。
  新增采样与 image copy 经 Lavapipe 的 Vulkan validation 检查，无警告或错误。
- CPU 原生映射边界与裁剪填充通过 ASan、UBSan 和 LeakSanitizer 检查。

架构修复补充回归：类型化 binding 直接绑定 AHB RGBA/YUV 输入和 BLOB 输出，
YUV AImage 在释放调用方 reader/resource 后由提交独立保活，结果及零 staging/readback 计数通过。
合法 C++ 字段名 `sample`、嵌套 Uniform/结构体和设备句柄的错误绑定案例通过 CPU/Vulkan 检查；
移动后的 Android SDK 消费者也通过直接句柄 attach 的编译与运行。

Android 测试 ELF 的 LOAD 对齐为 16 KB；本次设备实际页大小为 4 KB，尚无 16 KB 设备运行结果。
Android Vulkan validation layer 未在本次启用。新增 CI 作交叉编译与主机工具检查，不包含连接设备测试，
本地结果也不代表远程 CI 已执行。
