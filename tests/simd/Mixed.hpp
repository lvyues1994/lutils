#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
using MismatchFunction = std::optional<std::size_t> (*)(std::uint8_t const *, std::uint8_t const *,
                                                        std::size_t) noexcept;
MismatchFunction baselineAddress();
MismatchFunction advancedAddress();
void baselineArithmetic(float *, float const *, float const *, float const *);
void advancedArithmetic(float *, float const *, float const *, float const *);
void baselinePacking(std::uint8_t *, std::uint8_t const *);
void advancedPacking(std::uint8_t *, std::uint8_t const *);
void baselineConversion(std::uint32_t *, float const *);
void advancedConversion(std::uint32_t *, float const *);
