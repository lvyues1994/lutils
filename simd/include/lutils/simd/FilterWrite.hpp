#pragma once

namespace lutils::simd {
// Both modes require output capacity >= input size and preserve selected order.
enum class FilterWrite {
    Exact,         // Preserve output[count, size).
    AllowOverwrite // Output[count, size) has unspecified contents.
};
} // namespace lutils::simd
