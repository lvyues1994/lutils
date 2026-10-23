# OpenGL 读回与 BGRA 转换性能

## 问题与修改

原来的 `downloadImage<RGBA8>()` 将每个字节除以 255 转为浮点，再通过通用像素接口
乘以 255、`round`、饱和转换回字节。GCC 13.3 的汇编保留了每像素 4 次 `round` 调用。
对于相同 UNorm8 编码的 RGBA → BGRA，实际只需交换 R/B 通道。

现在同存储类型、同编码、同通道数的 UNorm/UInt/SInt 下载直接复制或交换通道。
交换使用字节访问，保留有符号表示和端序，也支持未对齐输入；GCC 在普通 `-O3`
配置下自动生成 16 字节 SIMD 循环，不增加 ISA 要求。SNorm、浮点、自定义像素和跨编码
仍使用原来的数值转换，保留 SNorm 最小码、NaN 等语义。

OpenGL 同步下载原先先把映射数据复制到快照，再复制到返回的 vector。
现在私有同步提交在唯一持有快照时移交 vector，4K 每帧减少一次 31.64 MiB 的分配和复制。
公开的异步 `Completion::readback()` 仍返回不可变快照。

独立 CPU 诊断中，1080p BGRA 转换约从 **23.43 ms 降至 0.60 ms**，4K 从
**93.49 ms 降至 3.01 ms**。原实现的分段诊断还测得 4K 的等待、映射和首次复制约
12.03 ms，快照到返回 vector 的额外复制约 9.05 ms。分段诊断与下面的完整路径测量
分开采集，其统计量不能直接相加。

## RTX 5070 Ti 实测

2026-10-23，Linux，i7-13700KF，NVIDIA GeForce RTX 5070 Ti，驱动 595.99.02，
OpenGL 4.3，GCC 13.3，C++17，`-O3 -DNDEBUG`。无窗口 EGL device 0，要求硬件后端。
场景是现有 camera → sphere tracer → visualize 三个 C++ 内核、两个球体。

计时范围：提交三个内核、等待完成、同步读回、转换为主机 BGRA8。
资源分配上传、内核编译、命令录制、CPU 对照和图片保存均在计时外。

| 分辨率 | 优化前 p50 | 优化后 p50 | 提速 | 优化前 p95 | 优化后 p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 960×720 | 7.942 ms | 0.684 ms | 11.61× | 8.070 ms | 1.900 ms |
| 1920×1080 | 24.040 ms | 1.992 ms | 12.07× | 24.318 ms | 3.119 ms |
| 3840×2160 | 113.018 ms | 17.507 ms | 6.46× | 129.797 ms | 18.197 ms |

每个进程先预热 10 帧，运行 CPU 对照，再预热 10 帧。每种分辨率分别采集 50 次
驻留计算和 50 次完整路径，交替次序，每对样本之间休息 1 ms（计时外）。
进程按旧、新、新、旧的 ABBA 次序执行；每个版本合并 100 个样本，p50 取中位数，
p95 取排序后的第 95 个样本。优化前使用独立保存的静态链接二进制；前后使用相同
场景、编译参数、预热与测量流程。

GPU 与桌面共享，未锁定频率，测量前后温度 47–48°C，因此保留 p95 反映波动。
这些数据是 CPU 观察到的完整同步延迟；驻留计算也会受执行间隔和 GPU 状态影响，
不能将它的变化归因为光追内核优化。基准输出的 `equivalent_fps_at_p50` 仅为
`1000 / p50_ms`，不是连续渲染吞吐。

三种分辨率、四个进程输出的 PPM 均逐字节一致；与 CPU 参考相比，各 BGRA 通道最大
误差保持在 1/255。新增测试覆盖 36 种格式、UNorm8/16 全部原始码、未对齐输入、
零长度和尾部、跨编码回退，以及重复下载的独立所有权。

回归结果：Linux 114 项通过，1 项 DMA 硬件测试跳过；ASan/UBSan 5 项通过；
CPU/Vulkan/OpenGL/GLES 共用下载路径的兼容性测试 5 项通过；NDK r28 的像素、
shader、异步和 OpenGL 契约测试目标交叉编译通过。Android 本轮未做真机测量。

## 复现

`benchmarks/opengl_readback.cpp` 保留完整路径基准。Linux 启用 OpenGL、image ops 和
benchmarks 后可构建；输出目录保存 CSV 样本、统计和三种分辨率的 PPM：

```sh
cmake --preset compute-opengl -DLUTILS_BUILD_BENCHMARKS=ON \
  -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build --preset compute-opengl --target opengl_readback_benchmark -j2
mkdir -p build/readback-results
env -u LIBGL_ALWAYS_SOFTWARE DISPLAY= WAYLAND_DISPLAY= \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  __GL_SHADER_DISK_CACHE=0 \
  build/compute-opengl/benchmarks/opengl_readback_benchmark build/readback-results
```

EGL vendor 路径按本机驱动配置选择。比较版本时，两边使用同一份基准源码，分别静态
链接各自的库，按 ABBA 次序运行；保存图片和 CPU 对照必须成功后才接受性能结果。
本轮原始记录在 `build/opengl-readback-6uUJ22/final/`，仅汇总报告进入文档。
