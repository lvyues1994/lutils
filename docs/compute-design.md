# C++17 计算与图像模块

## 范围与模块

Linux 首发，公共 C++17 API 为 Windows 保留可移植的数据与生命周期契约。
构建期使用 Clang 18 AST 将受限 C++17 内核转为带类型的中间表示，再生成 GLSL；
glslang 生成 Vulkan 1.1 SPIR-V，SPIR-V Tools 校验。运行库不链接 Clang。
CPU 包装和 Shader 由同一次构建生成，原算法仅维护一份 C++。
CPU 包装为独立编译单元，公共生成头只包含声明和参数值类型。语义宏通过
lutils_add_kernel 的 DEFINITIONS 同时传给两条构建路径；目录/目标定义、配置宏（包括
Release 的 NDEBUG）及常规 -D/-U 按编译顺序同步。语言条件表达式、强制包含等无法
可靠同步的设置报配置错误。内核禁止用编译器/平台内建宏改变语义。
初版内核生成使用单配置生成器。Clang depfile 记录传递包含关系。
多个入口共享源码时使用 inline/内部链接定义；CLI 拒绝输出覆盖输入源码。

| 模块 | 职责 | 依赖 |
| --- | --- | --- |
| image | 固定块线性格式、颜色描述、平面视图、布局验证、HostFrame | core 的 Result |
| compute/kernel | Invocation、只读/只写 word buffer 及 CPU 语义 | C++17 标准库 |
| tools/kernelc | AST 校验、类型明确的 IR、GLSL 与 CPU 包装生成 | Clang/LLVM，仅构建期 |
| compute/runtime | KernelSource、Buffer、Kernel、CommandList、Device、Completion | core、kernel |
| compute/backends | CPU 参考执行和 Vulkan 单计算队列 | runtime，Vulkan 后端私有依赖 loader |
| image/ops | 转换计划、设备帧、打包传输和图像内核 | image、runtime、生成内核 |

core 仅提供各模块共用的结果头文件。原 erasure 模块独立，不把动态分发放进像素循环。

## 图像契约

FormatDesc 是拥有描述数据的值。PlaneFormat 的块尺寸使用全分辨率图像坐标；
SampleBits 将组件样本的值位映射到块的物理位，物理位按地址顺序、每字节 LSB 优先编号。
这允许描述多平面、交错、子采样和分段位打包，不依赖封闭的 FormatId 分支。
命名格式是描述预设；格式可被描述，不代表每个转换均已实现。

FrameDesc 单独保存尺寸、颜色矩阵、范围、色域、传递函数、色度位置及扫描方式。
颜色未知可以保存，但算子必须拒绝其所需的未知信息。
视图保存 allocation base、capacity、row0 和带符号 stride；允许负 stride、非零 offset、
多个平面共享 allocation。访问区间用检查过的无符号计算验证，避免 PTRDIFF_MIN 取负。
HostFrame 拥有内存，view() 即时生成借用关系；视图不可超出所引用存储的生命周期。

奇数尺寸逐格式处理：NV12/I420 平面向上取块；YUYV/UYVY 要求偶宽。
设备 word 打包使用独立布局，各平面起始位置与行 stride 对齐四字节，尾部空间属于本平面。
上传初始化设备 padding，下载只覆盖主机有效行字节，保留调用方 padding。
小端主机按有效行字节 memcpy，其他字节序保留逐字节打包路径。

## 内核与编译器契约

入口是构建配置指定的普通函数，参数依次为 Invocation、一个或多个 ReadBuffer/WriteBuffer、
最后一个扁平参数结构。buffer 元素为 uint32 word；参数字段限 int32/uint32/float，最多 128 字节。
包装代码逐字段打包，不复制主机结构体 ABI。资源顺序决定 binding；读写权限进入 Shader 声明。

第一版目标子集：标量、局部变量、分支、循环、静态可解析的标量辅助函数、显式转换。
工作组 X 在构建时由 LOCAL_SIZE_X 指定，缺省为 1，Y/Z 固定为 1。
kernelc 同时生成 GLSL LocalSize 和主机元数据；Vulkan 拒绝两者不一致及超出设备能力的包。
dispatch extent 表示实际工作项总量，各维度必须整除工作组大小。
图像算子默认 X=32，向上补齐网格并检查溢出，内核用真实尺寸拒绝尾部工作项。
CPU 执行相同补齐网格；当前没有组内同步语义。
Params 禁止默认成员初始化、union、const/volatile 字段；修改表达式只允许在独立语句或 for 递增处。
SDK 的资源方法按声明身份识别为内建操作，校验不进入其 CPU 实现。
对辅助函数递归校验并拒绝调用环；未知 AST 节点默认报错，诊断保留源码位置。
暂不提供任意指针、动态对象、异常、虚调用、STL、原子、共享内存和线程屏障。
向量、数组、用户模板的扩展以新的正反测试为前提。

整数算法需避免 signed overflow、负数右移、非法移位和除零。
浮点不保证跨 CPU/GPU 位级一致；每个算法单独声明误差范围。
每个 invocation 必须独占实际写入的完整 word；编译器不承诺证明任意索引表达式互斥。
CPU bounds 检查帮助诊断，GPU 正确性依赖内核范围契约与差分测试。

## 执行与所有权

Device 工厂返回隐藏实现的接口；Buffer/Kernel 通过共享句柄支持跨提交复用。
CommandList 按顺序保存 Upload、Dispatch、Readback。dispatch 复制参数并保留资源；
upload 的左值输入复制，右值输入转移所有权，转移后不能通过旧指针或引用修改该存储。
提交复制命令描述并共享已拥有的上传数据，之后可以销毁或重新录制原列表。
不同 Device 的资源不得混用；整个列表在执行前检查资源归属。
同一 dispatch 的可写绑定不得重叠。

readback 返回不透明 ReadbackToken；Completion::readback(token) 等待并取得只读结果快照。
同一列表可以重复提交，token 随列表复制，但每次提交分别保存回读结果。
空 token 和不属于该提交的 token 返回 InvalidArgument。
快照可以比 Completion 活得更久；旧 Completion 的结果和时间戳不受执行槽复用影响。
CPU 同步解释相同命令流，Vulkan 将它们录进同一命令缓冲区。

同一 Device 及其所有 Completion 的方法调用必须由调用方统一串行化。
异步表示提交后可以继续准备和提交其他帧；当前接口不提供线程安全保证。
ready() 可能保存已完成的回读结果，因此它也会修改后端资源池。
单计算队列使用保守的 transfer/compute/host 屏障保持跨命令、跨提交的依赖；
这允许主机准备与设备执行交叠，不承诺 DMA 引擎与计算引擎并行。

### Vulkan 内存与执行槽

`VulkanOptions::deviceLocal` 默认 false；true 时 storage buffer 要求 DEVICE_LOCAL，
通过单独的 host-visible staging 上传和回读。false 保留 host-visible storage 选择。
`preferHostCached` 仅影响 host-visible storage 的类型偏好，默认 false；
上传 staging 偏好 coherent，回读 staging 偏好 cached，必要时执行 flush/invalidate。
新 buffer 逻辑内容为零；device-local buffer 首次提交使用前由 vkCmdFillBuffer 初始化。

每个 Submission 保存本次命令、回读快照和时间戳；SubmissionSlot 拥有命令池、命令缓冲区、
fence、描述符池、可选 query pool，以及上传/回读 staging。fence 完成后先保存数据和时间，
再将槽归还 SlotPool。槽下次使用时先重置命令池，再重置描述符池和 fence。
描述符池与 staging 容量不足时扩容；已完成句柄不占用执行槽。

`reuseSubmissionResources` 默认 true。`maxInFlight` 默认 4，必须大于零；达到上限时，
submit 等待并回收最早提交，再接受新任务，避免无限累积。
`maxCachedStagingBytes` 默认 64 MiB，限制空闲槽保留的 staging 有效容量；
在途 staging、分配对齐开销、调用方保留的结果快照不计入该缓存预算。
`VulkanStatistics` 统计执行槽、描述符池、staging buffer 的累计创建次数。
复用这些资源不等于零分配：每帧仍有打包数据、回读结果和命令描述的主机分配。

VulkanDevice 保留 pending submissions，Submission 不反向持有 VulkanDevice。
丢弃 Completion 不会释放未完成任务需要的资源。Device 销毁时等待全部提交；
仍持有的 Completion 随后可以读取已保存的结果。
同步 Device::upload/download 在返回前回收此前队列任务。
已完成失败记录从 pending 移出，不让一条旧错误永久阻塞后续提交；原 Completion 保留该错误。

验证选项包含显式硬件要求、同步验证和时间戳。ValidationReport 跨设备生命周期累计 callback；
须在所有资源释放后检查 errors。VulkanCompletion::elapsedNanoseconds() 返回整条命令的
设备区间，包含上传、dispatch、回读及屏障（按实际录制内容）。query 在完成回收时读取，
因此开启时间戳会把查询读取开销计入主机等待；正式性能测量关闭它，单独运行诊断。

正常错误返回 Result；标准容器的分配失败仍可能抛 std::bad_alloc。
失败的 CPU 内核可能已写出部分结果，调用方应丢弃该输出。
API 不自动回退 CPU，避免隐藏后端错误。

## 转换与验收样例

ConversionPlan 固定输入/输出描述、采样政策及数值规则，复用内核；DeviceFrame 组合图像描述与
runtime Buffer。同步主机入口与设备命令记录复用同一计划。
recordUpload 即时打包，不保留主机视图；recordReadback 返回 FrameReadback，保存描述、
设备布局与 token。copyTo(completion, view) 在调用时验证并借用目标内存，只写有效行字节。
ConversionPlan::run 将上传、转换、回读合并为一次提交。
多帧调用方循环复用若干组 DeviceFrame，每个槽收取上次结果后再用于新帧；
可运行实现见 examples/image_compute.cpp 和 benchmarks/uyvy_nv12.cpp 的 runPipeline。

YUYV422/UYVY422→NV12 验收：progressive、偶宽、可奇高；Y 原样保留，垂直 UV 用
(a+b+1)/2；奇高末行复制最后一行，输出垂直 chroma location 为 midpoint。
输入水平位置必须已知、垂直位置为 cosited；其余颜色信息保留。
每个工作项负责一组完整输出 word。

NV12→RGBA8 验收：BT.601 limited，保持色域及传递函数，最近邻色度重建，alpha=255。
输入水平位置必须已知、垂直位置为 midpoint；输出 chroma location 为 Unknown。
采用明确的整数系数、舍入和饱和；不会暗中把结果标成 linear/sRGB。
这两个算子用于证明多平面、重采样与计算链路，不封闭格式模型。

## 验证

格式层：自定义描述、负 stride、尾行恰好容量、奇数尺寸、溢出及非法映射。
编译器：真实 AST 正反案例、GLSL/SPIR-V 校验、生成 wrapper 与 CPU 结果。
运行时：外设备句柄、参数数量、alias、空工作量、上传快照、token 隔离、重复提交、
逆序等待、在途上限、旧完成句柄、设备提前销毁、资源计数稳定、描述符扩容及 staging 缓存上限。
算法：手算固定样例、独立标量参考、CPU/Vulkan 差分和主机 padding 保留。
性能报告需区分端到端和驻留执行；软件 Vulkan 结果不作为硬件 GPU 加速证据。
