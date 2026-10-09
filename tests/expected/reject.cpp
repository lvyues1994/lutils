#include "Test.hpp"

#if REJECT_CASE == 1
ex::expected<int &, int> invalid;
#elif REJECT_CASE == 2
ex::expected<int, int const> invalid;
#elif REJECT_CASE == 3
auto invalid = ex::expected<int, int>{}.and_then([](int) { return ex::expected<int, long>{}; });
#elif REJECT_CASE == 4
auto invalid = ex::expected<int, int>{}.or_else([](int) { return ex::expected<long, int>{}; });
#elif REJECT_CASE == 5
auto invalid = ex::expected<int, int>{}.transform([](int const &x) -> int const & { return x; });
#elif REJECT_CASE == 6
auto invalid = ex::expected<int, int>{}.transform_error([](int) {});
#elif REJECT_CASE == 7
auto invalid = ex::expected<int, std::unique_ptr<int>>{}.value();
#elif REJECT_CASE == 8
struct Throwing {
    explicit Throwing(int) noexcept(false) {}
};
auto invalid = ex::expected<Throwing, int>{std::in_place, 1}.emplace(2);
#elif REJECT_CASE == 9
ex::unexpected<int const> invalid{1};
#elif REJECT_CASE == 10
auto invalid = ex::expected<int, int>{}.and_then([](int) { return 2; });
#elif REJECT_CASE == 11
ex::expected<int[2], int> invalid;
#elif REJECT_CASE == 12
void invalid() {
    ex::expected<void, CopyOnly> left, right;
    left = std::move(right);
}
#endif
