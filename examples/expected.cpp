#include <charconv>
#include <iostream>
#include <lutils/Expected.hpp>
#include <string>
#include <string_view>
#include <system_error>

using Dimension = lutils::expected<unsigned, std::string>;

Dimension parseDimension(std::string_view const text) {
    if (text.empty())
        return lutils::unexpected{std::string("尺寸不能为空")};
    unsigned value{};
    auto const parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} or parsed.ptr != text.data() + text.size())
        return lutils::unexpected{std::string("尺寸必须是 unsigned 范围内的整数")};
    return value;
}

int main(int const argc, char **const argv) {
    auto const width = parseDimension(argc > 1 ? argv[1] : "1920")
                           .and_then([](unsigned const n) -> Dimension {
                               if (n == 0)
                                   return lutils::unexpected{std::string("尺寸必须大于零")};
                               return n;
                           })
                           .transform([](unsigned const n) { return std::to_string(n) + " px"; });
    if (not width) {
        std::cerr << width.error() << '\n';
        return 1;
    }
    std::cout << *width << '\n';
}
