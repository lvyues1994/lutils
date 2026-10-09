# expected：C++17 中的值与错误

头文件 `<lutils/Expected.hpp>`，CMake target 为 `lutils::core`，无需第三方依赖。
公共类型放在 `lutils` 命名空间，保留标准库的接口拼写。

## 标准基线

基础接口与“始终持有值或错误”的保证依据
[P0323R12](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0323r12.html)；
四个链式操作及 `error_or` 依据
[P2505R5](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r5.html)。
采用 C++23 最终命名 `unexpected::error()`。

实现也吸收了这些缺陷修正：

- [LWG3836](https://cplusplus.github.io/LWG/issue3836)：转换到 `expected<bool,E>` 时保留源状态，并转换成功值。
- [LWG3843](https://cplusplus.github.io/LWG/issue3843)、[3940](https://cplusplus.github.io/LWG/issue3940)：`value()` 对错误复制和引用限定的要求。
- [LWG3866](https://cplusplus.github.io/LWG/issue3866)、[3877](https://cplusplus.github.io/LWG/issue3877)：链式操作的返回类型与构造约束。
- [LWG3938](https://cplusplus.github.io/LWG/issue3938)、[3973](https://cplusplus.github.io/LWG/issue3973)：链式操作直接访问存储，支持移动专用错误，并避开解引用的 ADL 干扰。
- [LWG3886](https://cplusplus.github.io/LWG/issue3886)：`value_or` 的默认模板参数允许 `value_or({})`。

## 使用

```cpp
#include <lutils/Expected.hpp>
#include <string>

using Result = lutils::expected<int, std::string>;

Result checkPositive(int n) {
    if (n <= 0)
        return lutils::unexpected{std::string("必须大于零")};
    return n;
}

auto result = checkPositive(21)
    .and_then([](int n) -> Result { return n + 1; })
    .transform([](int n) { return n * 2; });
// result 持有 44；错误会跳过后续成功回调。
```

只报告成败时使用 `expected<void, E>`：默认构造表示成功，
`expected<void, E>{lutils::unexpect, ...}` 原位构造错误。
完整命令行示例见 [expected.cpp](../examples/expected.cpp)。

| 操作 | 接口与约定 |
| --- | --- |
| 构造 | 默认成功、从值构造、`unexpected<E>`、`std::in_place`、`unexpect`；支持 initializer_list |
| 状态 | `has_value()`、显式 `operator bool()` |
| 访问 | `*`、`->`、`value()`、`error()`，保留 const 与左右值类别 |
| 默认值 | `value_or`、`error_or`，参数会先求值；需要惰性恢复时用 `or_else` |
| 修改 | 值/错误赋值、`emplace`、成员与 ADL `swap` |
| 比较 | `==`、`!=`，可比较 expected、值、unexpected；没有大小排序 |
| 继续计算 | `and_then(f)`，回调返回 expected，保持错误类型 |
| 错误恢复 | `or_else(f)`，回调返回 expected，保持值类型 |
| 变换 | `transform(f)` 变换值，可返回 void；`transform_error(f)` 变换错误 |

链式接口提供 `&`、`const &`、`&&`、`const &&` 重载。
移动专用成员通常需要 `std::move(result)`，让未执行分支的成员也能转移到返回结果中。
`transform` 与 `transform_error` 直接构造结果成员，可返回不可复制、不可移动的对象。
回调支持普通函数、函数对象及 `std::invoke` 支持的成员指针。

`*` 和 `->` 要求当前成功，`error()` 要求当前失败；Debug 下用 assert 检查。
`value()` 失败时抛出携带错误的 `bad_expected_access<E>`，可通过
`bad_expected_access<void> const &` 统一捕获。按标准约束，`value()` 要求 E 可复制；
移动专用错误应在检查状态后使用 `*` 或链式接口。

## 存储与异常保证

`Expected.hpp` 管理公开接口、转换约束、观察器与链式操作；
`detail/ExpectedStorage.hpp` 管理 union 成员的生命周期、特殊成员函数及回滚。
对象自身不动态分配内存，值和错误共用内联存储，另有一个状态标记。
`T == E` 合法，`cv void` 由内部空占位类型表示成功状态。

复制/移动构造和析构随 T、E 的能力条件启用，并保留其条件平凡性。
复制/移动赋值按 C++23 基线实现，不承诺传播平凡性。
跨状态赋值只在能够安全构造新成员或无抛恢复旧成员时开放；同态赋值的保证取决于成员类型。
`emplace` 仅允许无抛构造，异状态 `swap` 会在失败时恢复被销毁的一侧。
操作抛异常后，对象仍有有效的值或错误；移动失败后，参与移动的成员内容遵循该成员自身的保证。

现有 `Result<T>` 继续供计算模块使用。本次新增 expected 不迁移这些 API；
两者的错误构造及访问契约不同，后续迁移需要逐个调用点检查。

## C++17 边界与验证

- 直接构造、条件平凡的复制/移动构造以及观察器可用于满足条件的常量表达式。
  C++17 不允许通过 placement new 在常量求值中切换 union 活跃成员，赋值、emplace、swap 不提供 C++23 的完整 constexpr 能力。
- 链式接口使用 C++17 的 `std::invoke`，调用回调的路径不能在 C++17 常量求值中执行。
- 条件 explicit 和部分条件成员通过 SFINAE 重载实现；不承诺与 std::expected 的成员函数指针类型或二进制布局相同。
- 支持对象值和 cv void，不支持引用值、数组值或 void 错误。要求异常支持。

测试包括类型特征、引用限定、短路行为、不可移动返回值、异常回滚和非法用法的编译诊断。
源码嵌入、安装与迁移安装目录后的下游测试也调用 expected。

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -R '^expected\.'
./build/debug/examples/expected_example 1920
```

同一组 traits、behavior、exceptions 测试可以定义 `LUTILS_EXPECTED_USE_STD`，
用 C++23 标准库进行语义对照。已知旧库差异：libstdc++ 13 的 bool 转换尚未修复，
libc++ 18 的 `value_or` 缺少默认模板参数；后者的花括号用例只对本实现单独验证。
