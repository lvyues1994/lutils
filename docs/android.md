# Android / NDK r28

使用 C++17 与 NDK r28c（28.2.13676358）。预设目标为 arm64-v8a、API 29，测试程序静态链接 libc++。
基础 image、CPU compute 和 Vulkan compute 均在 Android 编译；kernelc、glslang 和 spirv-val 在 PC 上运行。
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
分别编译预编译算子消费者和使用主机 kernelc 的消费者，再到设备运行。

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

普通 VA 使用 CPU 路径。Android 原生 RGBA/YUV AHB、AImage 和裸 dma fd 尚未作为图像导入接口提供；
原生图像需要真实的格式、pixel stride、外部图像能力及生产者同步信息，不能标记为 BLOB。

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

2026-10-10，NDK r28c、arm64-v8a、编译 API 29；设备为 Android 16 / API 36、Adreno 830：

- Android CTest 共 83 项：82 项通过，1 项 Vulkan FP16 测试因设备能力不足跳过。
  运行测试通过 adb 在设备执行，编译拒绝与 kernelc 测试在 PC 执行。
- 不启用 Vulkan 和生成算子的 `android-arm64` 基础预设另有 33 项测试，全部通过。
- 普通 CPU/Vulkan 转换、裁剪、填充及现有 shader 测试通过。设备缺少 Float64，已验证内核创建返回
  Unsupported，然后继续执行其余 shader 测试。
- AHB BLOB 的 RGBA8、UYVY、YUYV、NV12、I420 裁剪与背景填充通过 CPU/Vulkan 对照；
  包括单块/多块分配、非紧密 stride、保护字节、IPC 句柄别名、连续提交和资源保活。
  GPU 路径的 staging/readback buffer 创建计数均为零。
- 同步测试包含 CPU 映射等待 GPU fence；失败 fence 另用 ioctl/poll 故障注入验证错误传播。
- 安装并移动 SDK 后，下游预编译算子和新生成 shader 均能交叉编译并在设备运行；已生成 TGZ SDK。
- Linux CTest 共 85 项：83 项通过，2 项真实 dma-buf 测试在沙箱跳过；随后在真实分配上分别补测
  CPU 和 RTX 5070 Ti Vulkan 路径，均通过，后者启用了 Vulkan validation。

Android 测试 ELF 的 LOAD 对齐为 16 KB；本次设备实际页大小为 4 KB，尚无 16 KB 设备运行结果。
Android Vulkan validation layer 未在本次启用。新增 CI 作交叉编译与主机工具检查，不包含连接设备测试，
本地结果也不代表远程 CI 已执行。
