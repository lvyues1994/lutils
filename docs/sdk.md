# 构建、安装与 SDK 接入

需要 C++17、CMake ≥3.21、Ninja。基础库只依赖标准库和系统线程设施。
Linux 计算工具链使用 LLVM/Clang 18；Vulkan 后端要求 loader、开发文件和 Vulkan 1.1 设备。
Ubuntu 24.04 的完整依赖可安装为：

```sh
sudo apt-get install ninja-build clang-18 llvm-18-dev libclang-18-dev libclang-cpp18-dev \
  glslang-tools spirv-tools libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers
```

| 预设 | 用途 |
| --- | --- |
| debug / release | 基础库、测试与示例 |
| clang-sanitize | 基础库 ASan/UBSan |
| compute-cpu | kernelc、内置图像算子和 CPU 测试 |
| compute-release | 完整计算、Vulkan、示例和性能基准 |
| compute-sanitize | 完整计算 ASan/UBSan，安装关闭 |
| cpu-tsan | 线程池/工作组执行的 ThreadSanitizer 测试 |
| sdk-release | 完整 SDK；测试、示例、基准可执行程序关闭 |

各预设都有同名 build preset；除 sdk-release 外也有 test preset。
非标准依赖路径可指定 `LLVM_DIR`、`LUTILS_CLANG_INCLUDE_DIR`、`LUTILS_CLANG_CPP_LIBRARY`、
`LUTILS_GLSLANG`、`LUTILS_SPIRV_VAL`。普通配置不会下载依赖。

## 从源码嵌入

```cmake
add_subdirectory(path/to/lutils)
target_link_libraries(app PRIVATE lutils::erasure lutils::image lutils::compute_cpu)
```

子项目默认不启用测试、示例、安装。需要新 shader 时，在 add_subdirectory 前设置
`LUTILS_BUILD_KERNELC=ON`，之后调用 `lutils_add_shader`。
需要 GPU 时设置 `LUTILS_ENABLE_VULKAN=ON` 并链接 `lutils::compute_vulkan`。
仓库开发告警和 sanitizer 不作为 SDK 的传递编译选项导出。

## 安装与打包

```sh
cmake --preset sdk-release -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build --preset sdk-release
cmake --install build/sdk-release --prefix "$PWD/build/sdk"
cpack --config build/sdk-release/CPackConfig.cmake -B build/packages
cpack --config build/sdk-release/CPackSourceConfig.cmake -B build/packages
```

输出一个 Linux 二进制 SDK TGZ 和 `lutils-0.2.0-source.tar.gz`。
SDK 包含公共头文件、静态库、CMake 导出、kernelc、构建辅助函数、文档和示例源码。
内置转换的生成内核已编入 image_ops，不需要向下游导出指向原仓库的生成头。
安装目录可以整体移动，也支持包含空格的路径。

二进制 SDK 使用当前构建机的编译器 ABI、标准库、libc 和 CPU 架构；它不承诺跨 Linux 发行版兼容。
kernelc 可执行文件运行时需要匹配的 libclang-cpp 18 和 libLLVM 18。
只调用预编译图像算子的应用不需要运行 kernelc、glslang 或 spirv-val。
第三方库未打入 TGZ；仓库当前未声明开源许可证，此本地包不添加许可证或第三方再分发授权。

## find_package

```cmake
find_package(lutils 0.2 CONFIG REQUIRED)
target_link_libraries(app PRIVATE lutils::erasure lutils::image lutils::compute_cpu)
```

配置时传 `-DCMAKE_PREFIX_PATH=/path/to/sdk`。
无组件请求时只加载基础目标：core、erasure、image、image_memory、compute_kernel、compute、compute_cpu。
`lutils::image` 提供 CPU 裁剪/填充；`lutils::image_memory` 提供 VA/dma-buf 资源与 CPU 执行器。
可选组件如下：

| 组件 | 目标与作用 |
| --- | --- |
| vulkan | lutils::compute_vulkan；查找系统 Vulkan |
| image_ops | lutils::image_ops；预编译图像转换、裁剪与填充，支持 CPU 或另选 Vulkan |
| kernelc | lutils::kernelc 和 lutils_add_shader/lutils_add_kernel；构建新内核 |

```cmake
find_package(lutils 0.2 CONFIG REQUIRED COMPONENTS image_ops kernelc vulkan)
lutils_add_shader(adder NAME typed_add SOURCE Adder.hpp ENTRY demo::FloatAdder)
target_link_libraries(app PRIVATE adder lutils::compute_cpu lutils::compute_vulkan)
```

glslangValidator 和 spirv-val 在调用内核构建函数时才查找。
缺失的 REQUIRED 组件让配置失败；OPTIONAL_COMPONENTS 不会强制引入 Vulkan。
包版本按同一 minor 版本匹配。0.2 改变了 CPU dispatch 接口与类型化字节布局；升级时重新生成内核包装并重编下游，
不混用旧版本静态库或生成文件。

## 自动化验证

`downstream.source` 使用真实 add_subdirectory；`downstream.install` 安装后移动前缀，再构建运行外部工程。
覆盖基础接入、仅 image_ops、全组件、新 shader、依赖头修改重建、可选/缺失组件。
`downstream.packages` 实际生成并解压两种 TGZ，验证二进制消费者、源码构建、完整源码消费者及再次打源码包。
这些测试检查安装接口不含原仓库路径。sanitizer 构建关闭安装，安装包单独通过 Release 测试。

`.github/workflows/linux.yml` 提供 GCC/Clang 基础任务、完整计算、ASan/UBSan/LSan、Vulkan 同步验证、SDK 打包和 CPU TSan。
CI 使用 Lavapipe；硬件 GPU 的性能与验证单独在设备上运行。工作流文件已加入仓库，远端结果须以实际运行记录为准。
