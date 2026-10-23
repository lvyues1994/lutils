// Private target emission helpers. The parameter packet remains the CPU/Vulkan
// ABI.
enum class ShaderTarget { Vulkan, OpenGl, Gles };
llvm::cl::opt<std::string> shaderTargets{"targets", llvm::cl::init("vulkan"),
                                         llvm::cl::cat(category)};
bool hasTarget(std::string const &name) {
    return ("," + shaderTargets.getValue() + ",").find("," + name + ",") != std::string::npos;
}
std::string version(ShaderTarget target) {
    if (target == ShaderTarget::Gles)
        return "#version 310 es\nprecision highp float;\nprecision highp int;\n";
    return target == ShaderTarget::OpenGl ? "#version 430 core\n" : "#version 450\n";
}
std::string packet(std::size_t words) {
    return "layout(std140,binding=0) uniform LutilsPacket { uvec4 words[" +
           std::to_string(std::max<std::size_t>(1, (words + 3) / 4)) + "]; } packet;\n";
}
std::string packetWord(std::size_t byte) {
    return "packet.words[" + std::to_string(byte / 16) + "][" + std::to_string(byte / 4 % 4) + "]";
}
std::string scalarWord(std::string const &type, std::size_t byte) {
    auto word = packetWord(byte);
    if (type == "uint")
        return word;
    if (type == "int")
        return "int(" + word + ")";
    if (type == "bool")
        return "(" + word + "!=0u)";
    if (type == "float")
        return "uintBitsToFloat(" + word + ")";
    if (type == "double")
        return "packDouble2x32(uvec2(" + word + "," + packetWord(byte + 4) + "))";
    return "float16_t(unpackHalf2x16(" + word + ")[" + std::to_string(byte / 2 % 2) + "])";
}
std::string cppString(std::string const &value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else {
            if (c == '\\' || c == '"')
                result += '\\';
            result += c;
        }
    }
    return result + "\"";
}
void glslArtifact(std::ostream &out, std::string const &object, ShaderTarget target,
                  std::string const &text, std::string const &reason) {
    out << object << ".glsl.push_back({lutils::compute::GlslTarget::"
        << (target == ShaderTarget::Gles ? "OpenGles31" : "OpenGl43") << ',' << cppString(text)
        << ',' << cppString(reason) << "});\n";
}
