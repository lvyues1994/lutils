#include <lutils/simd.hpp>
namespace s = lutils::simd;
using A = s::abi::scalar;
int main() {
#if REJECT_CASE == 1
    s::Batch<bool, A> value;
#elif REJECT_CASE == 2
    s::Batch<const int, A> value;
#elif REJECT_CASE == 3
    auto value = s::shuffle<1>(s::Batch<int, A>{});
#elif REJECT_CASE == 4
    auto value = s::widen<std::int64_t>(s::Batch<std::uint32_t, A>{});
#elif REJECT_CASE == 5
    bool value = s::Mask<int, A>{};
#elif REJECT_CASE == 6
    auto value = s::Batch<int, A>{1} / s::Batch<int, A>{2};
#endif
    (void)value;
}
