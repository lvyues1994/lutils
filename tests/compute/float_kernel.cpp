#include <lutils/compute/Kernel.hpp>
namespace float_test {
namespace k = lutils::compute::kernel;
struct Params {
    float scale;
    k::I32 offset;
    k::U32 count;
};
inline float evaluate(float x, float scale) {
    auto value = x * scale;
    if (value < 0.0f)
        return 0.0f;
    return value;
}
inline void run(k::Invocation id, k::ReadBuffer input, k::WriteBuffer output, Params p) {
    if (id.x >= p.count)
        return;
    auto value = evaluate(static_cast<float>(input.load(id.x)), p.scale);
    auto rounded = static_cast<k::I32>(value) + p.offset;
    output.store(id.x, static_cast<k::U32>(rounded));
}
} // namespace float_test
