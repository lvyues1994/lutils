#include <lutils/simd.hpp>
namespace s = lutils::simd;
using A = s::abi::scalar;
constexpr std::size_t invalidIndex(std::size_t) { return 10; }
constexpr int negativeIndex(std::size_t) { return -1; }
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
#elif REJECT_CASE == 7
    s::Vec<float, 0, A> value;
#elif REJECT_CASE == 8
    s::Vec<float, 65, A> value;
#elif REJECT_CASE == 9
    auto value = s::permute<3, invalidIndex>(s::Vec<float, 3, A>{});
#elif REJECT_CASE == 10
    auto value = s::permute<3, negativeIndex>(s::Vec<float, 3, A>{});
#elif REJECT_CASE == 11
    auto value = s::bit_cast<std::uint32_t>(s::Vec<std::uint8_t, 3, A>{});
#elif REJECT_CASE == 12
    auto value = s::split<3>(s::Vec<float, 3, A>{});
#endif
    (void)value;
}
