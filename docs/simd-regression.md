# 固定硬件 SIMD 性能回归

主流程在同一台机器、同一任务中构建前后版本，覆盖同一份候选版本的 `benchmarks/simd/`，
再交错运行独立进程。库内部源文件、编译参数和链接布局可以变化，并记录在报告中。
不需要维护跨系统升级的固定性能基线。

## 本地使用

需要 Git、CMake、Ninja、C++ 工具链、uv，以及已安装的 Python 3.12 或更新版本。
准备脚本要求两个提交不同，且 before 是 after 的祖先。输出目录必须不存在；当前工作区不会被修改。

```sh
uv run --no-project --offline --python '>=3.12' tools/simd/prepare_pair.py \
  --before <before-commit> --after <after-commit> --output /absolute/private/pair

uv run --no-project --offline --python '>=3.12' tools/simd/pair.py capture \
  --before-build /absolute/private/pair/before --after-build /absolute/private/pair/after \
  --before-revision <before-commit> --after-revision <after-commit> \
  --suite filters --cpu 2 --machine-id my-fixed-runner \
  --output /absolute/private/pair/filters

uv run --no-project --offline --python '>=3.12' tools/simd/pair.py compare \
  /absolute/private/pair/filters --output /absolute/private/filter-report.json
```

准备阶段使用 Release、C++17、单配置 Ninja，先完成两边的 SIMD 正确性和 smoke 测试。
共同基准若不能编译旧接口，会明确失败，不会换回旧计时程序或删除不兼容用例。
也可以手动准备两份构建目录；采集期间不能重建目录。

| suite | 测量内容 |
| --- | --- |
| `filters` | u8/u16/u32/u64，精确写入与允许覆盖尾部两种过滤策略 |
| `extended` | 上述过滤与 float → i32/u32 四种饱和舍入 |
| `primitives` | 原有通用原语与 HostFrame 图像转换 |
| `va` | VA 资源，含映射、计算、结束访问 |
| `dma` | `/dev/dma_heap/system` 分配的真实 DMA buffer |
| `ahb` | Android AHB BLOB |
| `ahb-rgba` | NV12 AHB BLOB 输入、原生 RGBA AHB 输出 |

Android 使用相同 NDK 分别构建两份源码，在 capture 中追加 `--serial`，并选择设备可用 CPU。
每次运行使用独立 UUID 临时目录，只对测试进程设置 affinity，结束后清理；不改调频或设备设置。
`--push-timeout` 默认 60 秒，可在 1–600 秒内调整，适用于无线 ADB 和较大的二进制。
单个基准执行期限为 50 秒；传输和清理另有界限。资源不支持返回 77，不算性能通过。

## 配对协议与判定

默认五个完整块，每个块启动四个独立进程，即每个版本十个进程：

```text
块 0：before → after → after → before
块 1：after → before → before → after
……
```

每个用例至少预热 50 ms、采九组内部样本。manifest 保存实际执行顺序、时间、角色、
原始文件哈希、二进制哈希和完整构建信息；未完成、重复、损坏或顺序错误的数据拒绝比较。
旧版连续采集不能重新编号冒充配对数据。

兼容性分两层：

- 严格相同：本次机器/系统/CPU/调频策略、编译器、共同配置、基准源码及其编译参数、
  测量协议、实际用例集合和 stride。采集结束再次核对环境、二进制和构建信息。
- 允许变化并报告：库内部 TU 的增加/删除、私有参数和最终链接命令。
  如果参数变化传播到计时或校验代码，则仍作为测量语义变化拒绝比较。

因此内核或 microcode 升级后，只需在升级后的机器上重新构建并测量两个版本；
同一次 session 内的环境变化仍然不允许。`selected_target` 是结果，不是用例身份。

一个块用四个进程各自的中位数计算
`sqrt(after₁ × after₂ / (before₁ × before₂))`，实现使用对数避免乘法溢出。
最终变化由五个块比值的中位数计算；报告也保留前后绝对耗时和全部进程中位数。
ABBA 降低顺序漂移影响，不能消除所有机器噪声。

默认退化门槛 5%，噪声门槛 3%：

- 轮内最大 MAD/中位数或块比值 MAD/中位数超过 3%，判为无法确定。
- 块比值去掉最大、最小值后的跨度超过中位数的 12%，也判为无法确定。
- 无明显噪声时，比值中位数超过 1.05，至少 80% 的块超过 1.05，
  且次小块比值也超过 1.05，才判为退化；证据不足则为无法确定。

这是工程判据，不是统计置信区间。退出码：`0` 通过，`1` 确认退化，`2` 无效/不兼容/无法确定或执行失败，
`77` 资源不支持。失败后检查原因，不自动重复到变绿。
`regression.py capture/compare` 保留为历史数据诊断入口，仍使用原有严格构建相等规则；CI 使用新配对入口。

## CI 接入状态

`.github/workflows/simd-performance.yml` 提供默认分支 push 和手动入口，**默认关闭**。
普通 push 比较事件的 before 与当前 SHA；手动可指定祖先提交，留空比较 HEAD 的父提交。
全零、缺失、相同或非祖先基线直接失败，不静默换基线。

启用需要：

1. 专用隔离 runner，group `lutils-performance`，labels `linux`、`x64`、`lutils-simd`。
   group 限制为此 workflow 的受保护默认分支，不接收不可信 PR。
2. 受保护 environment `simd-performance`，限制默认分支。
3. 预装 Git、工具链、CMake、Ninja、uv、Python ≥3.12；离线 uv 能找到该解释器。
4. 配置 `LUTILS_SIMD_CPU`、`LUTILS_SIMD_MACHINE`，最后设置 `LUTILS_SIMD_PERF_ENABLED=true`。
   不再需要 `LUTILS_SIMD_BASELINE`。

任务串行执行，90 分钟总上限。每版 configure/build/test 分别限 60/300/60 秒；
超时或中断会终止自己创建的进程组。没有匹配测试视为失败。
报告与原始数据分别上传为 CI artifact，原始数据保留 14 天，不写入 Git；最后清理本次独立临时目录。
本次仅实现和本地验证，没有注册 runner、修改仓库设置或启用任务。
