# 图像裁剪与填充

`crop` 将输入矩形复制到同尺寸输出；`pad` 用指定背景填满输出，再将完整输入放到指定位置。
输入输出格式、颜色信息必须相同，首版要求逐行扫描，不缩放、不混合 alpha、不隐式转换格式。
坐标以左上角为原点，单位是完整图像像素。空区域、越界、输入输出存储重叠直接报错。
源图像与行尾 padding 不变。支持 RGBA8、UYVY/YUYV422、NV12、I420；CPU 也能按格式块复制自定义布局。

裁剪起点、尺寸和放置位置必须对齐每个平面的采样块：422 水平对齐 2，420 水平、垂直对齐 2。
填充画布可以有格式允许的奇数边缘；末尾不完整块使用背景值。背景使用目标颜色空间的原生
8 位无符号分量，每个分量必须出现一次，例如有限范围 YUV 黑色为 Y=16、Cb=128、Cr=128。

## VA 与 CPU

链接 `lutils::image` 可直接处理已有的 `ConstFrameView` / `FrameView`，支持非零 row0 和负 stride：

```cpp
#include <lutils/image/Region.hpp>
auto cropped = lutils::image::crop(inputView, {64, 48, 320, 240}, croppedView);
auto padded = lutils::image::pad(croppedInputView, {160, 120},
    {{lutils::image::Component::Y, 16}, {lutils::image::Component::Cb, 128},
     {lutils::image::Component::Cr, 128}}, outputView);
```

调用者检查 `Result` 的错误。重复执行时使用 `RegionPlan::crop/pad` 提前规划，再调用 `run`。
完整例子见 `examples/image_regions.cpp`。

## 统一资源与 Linux dma-buf

链接 `lutils::image_memory`。`ImageResource` 将图像描述、内存对象和平面布局分开：一个 fd 可以
容纳多个平面，多 fd 也能描述一张图。`PlaneLayout::object` 索引内存对象，offset/stride 使用字节。
普通 VA 通过 `ImageResource::borrow` 借用；其分配必须存活到任务完成。
若需要保活，可用 `compute::borrowMemory(..., owner)` 后调用 `ImageResource::linear`。

Linux 外部图像必须明确声明线性 modifier，不能将未知或 tiled/compressed 布局当作线性内存：

```cpp
#include <lutils/image/LinuxImage.hpp>
namespace im = lutils::image;
im::DmaBufImageDesc desc;
desc.image = frameDescription;
desc.fds = {fd};
desc.planes = {{0, yOffset, yStride}, {0, uvOffset, uvStride}};
desc.modifier = 0; // DRM_FORMAT_MOD_LINEAR，来自生产者的明确布局
auto image = im::importDmaBufImage(desc);
```

库复制 fd 并设置 CLOEXEC，调用者原 fd 不会被消费。重复 fd 按底层对象合并。
同一存储的不同 VA 映射无法仅凭指针识别；调用者必须避免此类源/目标别名。
已有 dma-buf 的 mmap 地址应保留 dma-buf 来源，不能作为普通 VA 导入以绕过同步。

`createCpuRegionExecutor()` 将资源直接映射后执行；`run` 阻塞完成，`submit` 返回已经完成的任务。
CPU 路径等待 dma-buf 先前访问结束，再执行 `DMA_BUF_IOCTL_SYNC START/END`；END 失败会返回错误。
`MemoryMapping::data()` 为只读访问，写操作必须取得 `writableData()`；映射在 finish 后不可再使用。

## GPU 执行与零拷贝

启用 kernelc/Vulkan，链接 `lutils::image_ops` 与 `lutils::compute_vulkan`：

```cpp
#include <lutils/image/DeviceRegion.hpp>
auto executor = im::createDeviceRegionExecutor(device);
auto input = im::bindImage(device, sourceResource);
auto output = im::bindImage(device, outputResource);
// 检查上述 Result 后，可反复使用已绑定资源：
auto done = executor.value()->submit(plan, input.value(), output.value());
if (done)
    done.value()->wait(); // 实际应用也应检查 wait 的 Result
```

GPU 执行器只记录 dispatch，直接读写资源原分配，不上传、不回读、不创建整帧中转 buffer。
普通 VA 没有默认 GPU 导入路径；不支持的内存、布局或能力返回错误，不切换 CPU。
已有设备 buffer 可经 `compute::bufferMemory` 和 `ImageResource::linear` 绑定。

首版 GPU 使用 uint 存储访问：正 stride，实际对象容量为 4 的倍数且不超过 32 位字节索引和设备限制。
offset 和 stride 无需 4 字节对齐。每个线程只拥有一个绝对目标 word；各平面顺序执行，保留有效区
以外的字节。背景填充与前景复制在同一次平面 dispatch 完成。源代码见 `image/ops/kernels/Regions.cpp`，
经现有 C++17 编译器生成 CPU 入口和 SPIR-V。

Vulkan 导入查询 DMA_BUF storage-buffer 能力、实际内存要求和 memory type；需要 foreign ownership
与 sync-file semaphore 支持。`DeviceInfo::capabilities.externalDmaBuf` 表示这些基础扩展和同步能力
可用，具体分配仍需导入验证。外部 buffer 禁止普通 upload/readback，也不会初始化清零。

## 同步与访问权

设备、执行器、同一资源的提交由调用者串行化；其他生产者在交接期间不得同时提交同一分配。
GPU 提交从 dma-buf 导出 acquire sync_file，导入等待 semaphore，执行 FOREIGN 队列 ownership
获取/释放，再将完成 fence 登记回所有输入和输出对象。输入登记读 fence，防止上游提前重写。
显式 fence 的生产者可通过 `DmaBufMemory::importSyncFile` 登记依赖；消费者可使用
`exportSyncFile` 导出其下一次访问应等待的 fence。此桥接需要内核的 EXPORT/IMPORT_SYNC_FILE ioctl。

Completion 保活命令与导入对象；引用保活不能替代摄像头/解码器队列的帧访问权，调用者仍需按生产者
协议等待或交还 release fence。GPU 已提交后若 fence 发布失败，库等待该任务结束后返回错误，输出可能
已改变；不会假装操作没有执行。CPU 同步或设备执行失败也不提供输出回滚。

Android 通过独立 `Memory` 适配器接入 AHardwareBuffer BLOB，详见 [Android 支持](android.md)。
原生 AHB RGBA/YUV 图像尚未接入；这类资源需要单独的图像布局与执行路径。

## 验证

```sh
ctest --test-dir build/compute-release -R '^image\.(regions|memory)' --output-on-failure
./build/compute-release/examples/image_regions_example
./build/compute-release/tests/dmabuf_region_tests --require-supported
./build/compute-release/tests/dmabuf_region_tests --vulkan --validation --require-supported
```

真实 dma-buf 测试优先使用 `/dev/dma_heap/system`，不可访问时尝试 GBM 的显式 LINEAR 分配。
可以传 `--heap PATH`、`--render PATH`；GBM 只用于可选测试，不是库的依赖。
无可用设备时 CTest 返回 77 标记跳过；`--require-supported` 将缺能力视为失败。
`image.memory.linux` 另用 ioctl 故障注入验证 fd 生命周期、EINTR/EAGAIN 重试和缓存同步清理，不作为硬件证据。

测试覆盖独立格式字节参考、全部分配的 padding/guard、正负 stride、非对齐平面、多次异步提交和
无 staging 计数；SDK 下游测试调用已安装的区域 API。

2026-10-10 本机验证：Release 全部 83 项中，81 项通过，2 项 dma-buf 测试因沙箱设备限制跳过；
这两项随后在真实设备环境补验通过。RTX 5070 Ti + GBM LINEAR 分配覆盖五种预设格式、单 fd
多平面和多 fd、裁剪后接填充及异步生命周期；Vulkan validation 无错误，未发生 staging。

CPU 与 Lavapipe 的 4 项 ASan/UBSan/LSan 检查通过；真实硬件的 6 项 ASan/UBSan 检查通过，
该次关闭了泄漏检测。硬件环境的 LSan 仍报告退出时残留：仅创建/销毁 GBM device 的独立程序
报告 112 字节，仅创建/销毁 Vulkan instance/device 的独立程序在 DBus 路径报告 2005 字节。
两份最小程序均未链接 lutils，因此不将这组结果记为硬件 LSan 通过；尚未区分驱动与依赖的责任。
