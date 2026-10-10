# 计算能力与 CPU 执行

0.2 保留 C++17 类内核写法，并增加共享内存、屏障、32 位原子操作和 FP16。
主机资源、图像和编译配置的基本约定见 [PDF 功能对齐](pdf-parity.md)。

## 共享内存和屏障

```cpp
struct LUTILS_KERNEL Sum {
    static constexpr char fileLocation[] = "sum";
    uvec3 local_size{8, 1, 1};
    BufferBinding<uint, 0> output;
    SharedArray<uint, 8> values;
    void main() {
        uint lane = gl_LocalInvocationIndex;
        values[lane] = gl_GlobalInvocationID.x;
        barrier();
        if (lane == 0u) {
            uint sum = 0u;
            for (uint i = 0u; i < 8u; ++i) sum += values[i];
            output[gl_WorkGroupID.x] = sum;
        }
    }
};
```

`SharedArray<T,N>` 是工作组内共享的数组，声明为 kernel 的公共成员，N 为正编译期常量。
每个工作组独立使用它；先写后读，不依赖初始值。共享元素可以是支持的标量、向量或值结构体。
编译器生成一份 shared 结构体，并计算共享内存预算；创建设备内核时检查上限。

`barrier()` 同步同组全部工作项及共享内存。当前编译器只接受 main 中直接出现的顶层屏障语句：
屏障不能在 if、循环、helper 或条件表达式中，含屏障的 main 不能提前 return。
普通分支和循环可以出现在屏障之间。共享数组和屏障内核的三维 dispatch 范围均须整除 local_size，
不完整工作组在录制阶段返回错误。

屏障不提供跨工作组同步，也不建立普通 buffer/image 写入的组内可见性。
跨工作组阶段通过多个 dispatch 表达；CommandList 保持这些阶段的顺序。

## 原子操作

支持 `atomicAdd/Min/Max/And/Or/Xor/Exchange/CompSwap`，返回修改前的值。
对象必须是 buffer 或 SharedArray 中的 int32/uint32 左值；局部变量、Uniform、浮点和 64 位值不接受。
例如 `auto previous = atomicAdd(counts[index], 1u);`。
同一位置有并发访问时，全部冲突访问都要使用合适的原子操作或共享内存屏障；普通读写不会自动变成原子。
atomicAdd 的 int32 加法按 32 位位模式回绕；其他普通 signed 运算仍须避免溢出。
带嵌套修改的参数和依赖实参求值顺序的表达式会被拒绝。

完整示例和 CPU/GPU 差分测试见 `tests/compute/workgroup.hpp`。

## FP16

`half` 存储 IEEE binary16，`f16vec2/3/4` 提供对应向量。
显式构造接受整数、float、double；转换回 float/double 也需显式转换。
CPU 转换使用 nearest-even，基本运算每步舍入到 binary16。
`half::fromBits` 和 `bits()` 仅供主机数据处理，不能在 shader 中调用。
long double 输入不接受，以免隐式经 double 转换发生双重舍入。

Buffer 和 Uniform 使用 std430 字节布局：half 大小/对齐为 2/2，half2 为 4/4，
half3 为 6/8，half4 为 8/8。数组元素按对齐后的 stride 存储。
生成 codec 逐字段编码，不要求主机结构体自身符合 std430。
`StorageCodec::bytes` / `alignmentBytes` 是布局依据；`words` / `alignment` 保留向上取整的旧单位。
三元素 half buffer 的逻辑长度是 6 字节，word 传输容器为 8 字节，padding 不计入元素数量。
低层接口通过 `createBufferBytes` / `Buffer::byteCount()` 表达这个区别。

Vulkan 分别查询并启用 FP16 算术、16 位 storage buffer 和 16 位 push constant 能力；
创建内核时按实际 SPIR-V capability 检查，不满足则返回 Unsupported。
`Device::info().capabilities` 提供这些能力及工作组/共享内存上限。
`VulkanOptions::enableFloat16=false` 可显式禁用半精度计算能力。
同一能力结构的 `float64` 表示 double 算术支持；不支持时，包含 Float64 capability 的内核创建返回 Unsupported。

R16F、RG16F、RGBA16F 是新增的原生图像格式；imageLoad/imageStore 仍使用 float32 向量。
这与 buffer 中真正的 float16 算术是两条能力路径。
GPU 的 NaN、次正规数、融合运算与数学函数可能和 CPU 不同，算法应声明误差容限；
半精度不会自动替换现有整数 YUV 算法。

## CPU 执行与错误

`createCpuDevice(CpuOptions{workers, threshold})` 控制普通类内核的工作线程数和并行阈值。
默认最多使用 8 个硬件线程，工作项少于 4096 时串行执行；workers=1 可选串行参考路径。
线程池由 Device 拥有并复用，各工作线程持有独立 kernel 实例与调用坐标。
上述默认值可以按实际 workload 调整；旧 ABI v1 函数入口仍使用串行参考执行。

共享内存/屏障内核使用每 lane 一个线程，同次 dispatch 内复用线程执行各工作组；
工作组依次执行。这条路径用于正确性验证，CPU 上限为 1024 lanes 和 64 KiB shared。
任一 lane 抛异常时会取消屏障、唤醒并加入其他线程，再返回原始错误。
普通线程池也会等待全部任务结束后传递错误，后续提交仍可运行。
CPU 原子操作通过 dispatch 的互斥锁保护，不把普通对象强制转换成 std::atomic。
同一 Device/Completion 的主机 API 调用仍要求调用方串行化。

## 验证入口

- `shader.half.values`：全部 65536 个编码、相邻有限值的舍入边界和整数通道归一化。
- `shader.half.cpu/vulkan`：实际 SPIR-V half 加乘、奇数长度、嵌套布局、Uniform、shared 和缺失能力。
- `compute.workgroup.cpu/vulkan`：二维多组归约、直方图、全部原子操作、旧值及 CAS。
- `shader.group.reject.*`：分歧屏障、提前返回、helper 屏障、非法原子目标、嵌套副作用。
- `compute.execution`：并发覆盖、原子争用、异常取消和错误后恢复；`cpu-tsan` 使用 TSan 跑这项测试。

完整构建继续对 36 种图像格式及原有转换、光线追踪、Game of Life 做回归。
GPU half 测试在设备确实缺失 FP16 时返回 CTest skip；缺失能力的拒绝测试仍先执行。
