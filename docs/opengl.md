# OpenGL / OpenGL ES 计算后端

Linux 支持 OpenGL 4.3 和 OpenGL ES 3.1；Android 使用 GLES 3.1，NDK r28、API 29 可编译。
链接 `lutils::compute_opengl`，包含 `<lutils/compute/OpenGl.hpp>`。C++17 内核、binding 和
`Backend::execute()` 的用法与现有后端相同：

```cpp
#include <lutils/compute/Compute.hpp>
#include <lutils/compute/OpenGl.hpp>
namespace co = lutils::compute;
co::OpenGlOptions options;
options.target = co::GlslTarget::OpenGles31; // Linux 也可选 OpenGl43
options.maxInFlight = 4;
auto result = co::createOpenGlDevice(options);
if (!result) { /* 处理 result.error() */ }
else {
    co::Backend backend{std::move(result).value()};
    // uploadBuffer / uploadImage、execute、downloadBuffer / downloadImage
}
```

默认后端选择保持原约定；需要 GL 的调用方显式创建设备。

## 编译链与模块

kernelc 对同一 C++ AST 分析一次，分别生成 CPU 包装、Vulkan GLSL、GLSL 430 或 ESSL 310。
`lutils_add_shader(... TARGETS cpu vulkan opengl gles)` 可选构建目标；CPU 包装始终生成。
GL-only 构建不生成或链接 SPIR-V。GL/GLES 文本由 glslang 离线检查，创建设备内核时再由驱动编译。
目标不支持的内核在 `KernelSource::glsl` 中保留原因，`createKernel()` 返回 `Unsupported`。

| 模块 | 职责 |
| --- | --- |
| kernelc / GlslTarget | 目标方言、资源声明、参数解码、诊断 |
| Context | EGL 显示与私有线程、函数加载、能力查询、编译诊断 |
| Resources | buffer/texture 生命周期、原生导入、外部采样器变体 |
| OpenGl | 命令预检、提交、内存屏障、完成通知、读回快照 |

参数保持原 CPU/std430 字节布局，通过 std140 `uvec4[]` UBO 传递，在 shader 内解码。
buffer、storage image、sampler 分别使用连续绑定编号，保留用户声明的逻辑 slot。
类内核 ABI v2 和旧函数 ABI v1 均支持；LocalSize 与元数据不一致时拒绝创建。

## 执行与生命周期

设备创建独立的无窗口 EGL context，所有 GL 调用和资源删除在其工作线程执行。
Linux 默认选择 surfaceless EGL，也可用 `deviceIndex` 选择 EGL 枚举设备；不创建桌面窗口。
EGL 需支持 1.5 或 `EGL_KHR_get_all_proc_addresses`。Android 默认 display 是进程共享对象，
设备析构只清理自己的 context/surface，不调用会影响宿主的 `eglTerminate`。
宿主也应避免在计算设备存活时终止该 display。

提交先校验全部命令，再依次执行。GPU 内存屏障保证资源可见性；GLsync 标识完成；外部
sync-file 单独处理所有权交接。达到 `maxInFlight` 时等待并回收旧提交。
每个 Readback 在其命令位置生成独立快照，读回数据在完成后映射。
Completion 和资源可超出 Device 生命周期；丢弃 Completion 不取消已提交命令。
Device 与 Completion 操作仍要求调用方串行化，资源句柄可在其他宿主线程释放。

## 能力范围

GL 目标支持当前 buffer、Uniform、共享内存、屏障、整数原子、采样、裁剪填充和图像算子。
桌面 GL 支持现有 36 种 image 格式及 1D/2D/3D；GLES 支持 2D/3D 与以下 storage 格式：
RGBA32F、RGBA16F、R32F、RGBA32UI、RGBA16UI、RGBA8UI、R32UI、RGBA32I、RGBA16I、RGBA8I、
R32I、RGBA8、RGBA8Snorm。同一 image 同时读写仅支持 R32F/R32UI/R32I，其余必须分开输入输出。
图像原始位读回通过兼容的 unsigned image 视图实现，保留 SNORM 极值和 NaN payload。

GL/GLES 原生 float16 算术暂不支持；GLES 不支持 float64。纹理格式 RGBA16F 与原生 half
算术是不同能力。资源、工作组、共享内存和内部读回容量均受设备实际限额约束。
`requireHardware=true` 会拒绝已识别的软件渲染器。

## 外部内存

| 来源 | 接入与条件 |
| --- | --- |
| 普通 VA | 使用显式 upload/readback 或 CPU 执行器；不能直接 import 为 GPU 内存 |
| Android AHB BLOB | `AndroidHardwareBufferMemory`；需 `GL_EXT_external_buffer`、`GL_EXT_buffer_storage`，整 word 容量 |
| Android 原生 RGBA AHB / AImage | `AndroidHardwareBufferImage` / 现有 AImage 适配；EGLImage 导入，需要 GPU sampling usage；写入还需 GPU color-output usage 与可写 storage image 支持 |
| Android 原生 YUV AHB / AImage | `GL_EXT_YUV_target` 与 external sampler；输出保持库的原始 `(Cr,Y,Cb,1)` 通道契约 |
| Linux 原生 DMA 图像 | `DmaBufNativeImage::import` / `image::importNativeDmaBufImage`；保存 FourCC、每平面 fd/offset/stride/modifier，查询 EGL modifier 支持 |

DMA 图像描述支持 DRM ABGR8888（内存 RGBA）、NV12、NV21、YUV420。导入持有 fd 副本；
线性布局允许 CPU 映射，其他 modifier 仅供原生 GPU 导入。EGL 标记 external-only 的布局
使用 external sampler；YUV 不经过隐式 RGB 转换。现有 `importDmaBufImage()` 继续提供线性
CPU/Vulkan 资源，裸 `DmaBufMemory` 不会被当作 GL 的 opaque-fd SSBO 导入。

外部访问要求 EGL native fence 与 server wait。读取和写入均发布 release fence，保留
AHB/AImage 生产者租约直到完成。原生 YUV 输出不支持 imageStore；可输出到支持的线性
buffer，再由生产者解释布局。外部资源禁止普通 upload/readback，不做隐藏 staging。
缺少扩展、格式或同步支持时返回 `Unsupported`。

## 构建与验证

Linux 额外需要 EGL/GLES 开发头与 EGL 库，例如 Ubuntu 的 `libegl1-mesa-dev`、
`libgles2-mesa-dev`。安装组件名为 `opengl`：

```sh
cmake --preset compute-opengl -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build --preset compute-opengl -j2
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 ctest --preset compute-opengl

cmake --preset android-opengl
cmake --build --preset android-opengl -j2
ctest --preset android-opengl -R '^downstream.android$'
```

Android 预设使用已有 host-kernel-tools；离线 SDK 测试只安装、搬迁并编译消费者。
设备连接后启用 `LUTILS_ANDROID_DEVICE_TESTS` 和指定序列号，运行 `ctest -L opengl`。
原生互操作测试在缺少必要扩展时返回 77 并报告原因，跳过不算实测通过。

Linux 同时安装多个 EGL 驱动时，可设置
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`
将软件渲染测试限制在 Mesa；路径以本机 GLVND 配置为准。

Linux GL/GLES 测试复用转换、区域、数学、共享内存/原子、图像格式与光追 CPU 对比，
另检查异步快照、设备隔离、跨线程释放、空执行网格、丢弃 Completion、原始位传输和错误诊断。
DMA 元数据、映射和同步错误路径另有模拟 ioctl 测试；它不替代真实 DMA 互操作验证。

读回路径的优化、RTX 5070 Ti 前后性能和复现方法见
[OpenGL 读回与 BGRA 转换性能](opengl-readback-performance.md)。
