# lutils

C++17 工具库。目前提供头文件形式的类型擦除模块 `lutils::erasure`，无第三方依赖。

## 类型擦除

定义一个接口模板，就可以让拥有对象和借用视图直接调用成员函数。具体类型无需继承接口。

```cpp
#include <lutils/erasure.hpp>

namespace te = lutils::erasure;

template <class Model>
struct ICounter : te::Interface<ICounter, Model, te::Extends<te::ICopyable>> {
    using ICounter::Interface::Interface;

    virtual int value() const { return te::value(*this).value(); }
    virtual void setValue(int n) { te::value(*this).setValue(n); }
};

struct Counter {
    int n = 0;
    int value() const { return n; }
    void setValue(int value) { n = value; }
};

auto counter = te::Any<ICounter>{Counter{10}};
auto copy = counter; // 按 Counter 的复制构造函数复制目标
auto borrowed = te::AnyPtr<ICounter>{&counter};
borrowed->setValue(20);
auto readOnly = te::AnyConstPtr<ICounter>{borrowed};
auto result = readOnly->value(); // 20，copy.value() 仍为 10
```

完整可运行示例见 [examples/counter.cpp](examples/counter.cpp)。

### 常用操作

| 操作 | 用法 |
| --- | --- |
| 原位构造 | `te::Any<ICounter>{std::in_place_type<Counter>, ...}` |
| 借用普通对象 | `te::AnyPtr<ICounter>{&object}` |
| 借用 const 对象 | `te::AnyConstPtr<ICounter>{&constObject}` |
| 判断空状态 | `te::empty(owner)`；视图也支持 `if (pointer)` |
| 释放目标／解除借用 | `te::reset(owner)` / `te::reset(pointer)` |
| 替换目标 | `te::emplace<Counter>(owner, ...)`，返回新目标的引用 |
| 交换 | `te::swap(a, b)` |
| 启用 SBO | `te::Any<ICounter, te::SmallBufferStorage<64>>` |

接口用 `Extends<IBase, ...>` 组合；借用视图可显式构造为基接口视图。接口仅包含行为，继承库提供的构造函数，不添加实例字段、自定义构造函数或特殊成员函数。`_te_` 前缀和 `detail` 命名空间属于内部实现。

`ICopyable` 允许复制和移动，`IMovable` 只允许移动；都不声明时，拥有对象只能原位构造和替换。视图始终可以复制，复制的是借用关系。详细的生命周期、异常和存储约定见 [模块设计](docs/erasure-design.md)。

## 构建与测试

需要 CMake 3.21 或更新版本；预设使用 Ninja。

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/examples/erasure_counter
```

另有 `release`、`clang-sanitize` 预设，分别验证优化构建和 AddressSanitizer／UBSan。若运行环境不支持 LeakSanitizer，可用 `ASAN_OPTIONS=detect_leaks=0 ctest --preset clang-sanitize` 单独关闭泄漏检测。

作为 CMake 子项目使用：

```cmake
add_subdirectory(path/to/lutils)
target_link_libraries(your_target PRIVATE lutils::erasure)
```

嵌入其他项目时，测试和示例默认关闭；编译告警和 sanitizer 选项只作用于本仓库的测试与示例。
