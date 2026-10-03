#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/MakeSupport.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
static_assert(CLANG_VERSION_MAJOR == 18, "kernelc requires Clang 18 headers");
using namespace clang;
llvm::cl::OptionCategory category{"lutils kernel compiler"};
llvm::cl::opt<std::string> entryName{"entry", llvm::cl::Required, llvm::cl::cat(category)};
llvm::cl::opt<std::string> outputBase{"output", llvm::cl::Required, llvm::cl::cat(category)};
llvm::cl::opt<std::string> packageName{"name", llvm::cl::Required, llvm::cl::cat(category)};
llvm::cl::opt<unsigned> localSizeX{"local-size-x", llvm::cl::init(1), llvm::cl::cat(category)};
std::string inputPath;
bool failed = false;

void checkOutput(std::string const &input, std::string const &output) {
    namespace fs = std::filesystem;
    if (fs::weakly_canonical(input) == fs::weakly_canonical(output) ||
        (fs::exists(output) && fs::equivalent(input, output)))
        throw std::runtime_error("output would overwrite input: " + output);
}

// Every expression retains the resolved scalar type. No source-text rewriting.
struct Expression {
    enum class Kind { Atom, Unary, Binary, Call, Cast, Conditional, Postfix };
    Kind kind;
    std::string type;
    std::string value;
    std::vector<Expression> children;
};
struct Statement {
    enum class Kind {
        Block,
        Declaration,
        Expression,
        If,
        For,
        While,
        Return,
        Break,
        Continue,
        Empty
    };
    Kind kind;
    std::string type;
    std::string name;
    std::vector<Expression> expressions;
    std::vector<Statement> children;
};
struct Function {
    std::string type;
    std::string name;
    std::vector<std::pair<std::string, std::string>> parameters;
    Statement body;
};
std::string expression(Expression const &e) {
    using K = Expression::Kind;
    auto child = [&](std::size_t i) { return expression(e.children.at(i)); };
    if (e.kind == K::Atom)
        return e.value;
    if (e.kind == K::Unary)
        return "(" + e.value + child(0) + ")";
    if (e.kind == K::Postfix)
        return "(" + child(0) + e.value + ")";
    if (e.kind == K::Binary)
        return "(" + child(0) + " " + e.value + " " + child(1) + ")";
    if (e.kind == K::Cast)
        return e.type + "(" + child(0) + ")";
    if (e.kind == K::Conditional)
        return "(" + child(0) + " ? " + child(1) + " : " + child(2) + ")";
    std::string text = e.value + "(";
    for (std::size_t i = 0; i < e.children.size(); ++i)
        text += (i ? ", " : "") + child(i);
    return text + ")";
}
std::string statement(Statement const &s) {
    using K = Statement::Kind;
    auto expr = [&](std::size_t i) { return expression(s.expressions.at(i)); };
    auto body = [&](std::size_t i) { return statement(s.children.at(i)); };
    if (s.kind == K::Block) {
        std::string text = "{\n";
        for (auto const &child : s.children)
            text += statement(child) + "\n";
        return text + "}";
    }
    if (s.kind == K::Declaration)
        return s.type + " " + s.name + (s.expressions.empty() ? "" : " = " + expr(0)) + ";";
    if (s.kind == K::Expression)
        return expr(0) + ";";
    if (s.kind == K::Return)
        return "return" + (s.expressions.empty() ? std::string{} : " " + expr(0)) + ";";
    if (s.kind == K::If)
        return "if (" + expr(0) + ")\n" + body(0) +
               (s.children.size() == 2 ? "\nelse\n" + body(1) : "");
    if (s.kind == K::For)
        return "for (" + body(0) + " " + expr(0) + "; " + expr(1) + ")\n" + body(1);
    if (s.kind == K::While)
        return "while (" + expr(0) + ")\n" + body(0);
    if (s.kind == K::Break)
        return "break;";
    if (s.kind == K::Continue)
        return "continue;";
    return ";";
}

struct Compiler {
    ASTContext &context;
    FunctionDecl const *entry;
    ParmVarDecl const *invocation = nullptr;
    ParmVarDecl const *params = nullptr;
    RecordDecl const *paramRecord = nullptr;
    std::map<ValueDecl const *, std::string> names;
    std::map<ParmVarDecl const *, std::size_t> bindings;
    std::vector<bool> writes;
    std::vector<FieldDecl const *> fields;
    std::map<FunctionDecl const *, std::string> functions;
    std::set<FunctionDecl const *> active;
    std::vector<Function> ir;
    unsigned nextName = 0;

    [[noreturn]] void reject(Stmt const *s, std::string const &message) const {
        auto loc = s ? s->getBeginLoc() : entry->getLocation();
        throw std::runtime_error(loc.printToString(context.getSourceManager()) + ": " + message);
    }
    [[noreturn]] void rejectDecl(Decl const *d, std::string const &message) const {
        throw std::runtime_error(d->getLocation().printToString(context.getSourceManager()) + ": " +
                                 message);
    }
    std::string scalar(QualType type) const {
        if (type.isVolatileQualified())
            rejectDecl(entry, "volatile kernel values are unsupported");
        auto t = type.getCanonicalType().getUnqualifiedType();
        if (t->isVoidType())
            return "void";
        if (t->isBooleanType())
            return "bool";
        if (t->isSpecificBuiltinType(BuiltinType::UInt))
            return "uint";
        if (t->isSpecificBuiltinType(BuiltinType::Int))
            return "int";
        if (t->isSpecificBuiltinType(BuiltinType::Float))
            return "float";
        rejectDecl(entry, "unsupported kernel type: " + type.getAsString());
    }
    RecordDecl const *record(QualType type) const {
        auto const *t = type.getCanonicalType()->getAs<RecordType>();
        return t ? cast<RecordDecl>(t->getDecl()->getCanonicalDecl()) : nullptr;
    }
    std::string name(ValueDecl const *d) {
        auto it = names.find(d);
        if (it != names.end())
            return it->second;
        auto value = "v" + std::to_string(nextName++);
        names.emplace(d, value);
        return value;
    }
    void signature() {
        if (entry->isVariadic() || entry->getNumParams() < 3 ||
            !entry->getReturnType()->isVoidType())
            rejectDecl(entry, "entry must return void and take Invocation, buffers, "
                              "Params by value");
        invocation = entry->getParamDecl(0);
        auto const *id = record(invocation->getType());
        if (!id || id->getQualifiedNameAsString() != "lutils::compute::kernel::Invocation")
            rejectDecl(invocation, "first parameter must be kernel::Invocation by value");
        for (unsigned i = 1; i + 1 < entry->getNumParams(); ++i) {
            auto const *p = entry->getParamDecl(i);
            auto const *r = record(p->getType());
            auto q = r ? r->getQualifiedNameAsString() : std::string{};
            if (q != "lutils::compute::kernel::ReadBuffer" &&
                q != "lutils::compute::kernel::WriteBuffer")
                rejectDecl(p, "resource parameters must be ReadBuffer or WriteBuffer by value");
            bindings.emplace(p, writes.size());
            writes.push_back(q == "lutils::compute::kernel::WriteBuffer");
        }
        params = entry->getParamDecl(entry->getNumParams() - 1);
        paramRecord = record(params->getType());
        auto const *cpp = dyn_cast_or_null<CXXRecordDecl>(paramRecord);
        if (!cpp || cpp->isUnion() || !cpp->isAggregate() || !cpp->isTriviallyCopyable() ||
            cpp->getNumBases())
            rejectDecl(params, "last parameter must be an aggregate with scalar fields");
        for (auto const *field : paramRecord->fields()) {
            auto t = scalar(field->getType());
            if (t == "bool" || t == "void" || field->isBitField() ||
                field->hasInClassInitializer() || field->getType().isConstQualified() ||
                field->getType().isVolatileQualified())
                rejectDecl(field, "parameter fields must be int32, uint32 or float");
            fields.push_back(field);
        }
        if (fields.empty() || fields.size() > 32)
            rejectDecl(params, "Params must contain 1 to 32 scalar fields");
    }
    bool mutates(Stmt const *s) const {
        if (!s)
            return false;
        if (auto const *u = dyn_cast<UnaryOperator>(s))
            if (u->isIncrementDecrementOp())
                return true;
        if (auto const *b = dyn_cast<BinaryOperator>(s))
            if (b->isAssignmentOp())
                return true;
        for (auto const *child : s->children())
            if (mutates(child))
                return true;
        return false;
    }
    Expression lower(Expr const *e, bool allowMutation = false) {
        using K = Expression::Kind;
        if (!allowMutation && mutates(e))
            reject(e, "nested mutation has no portable evaluation order");
        if (auto const *p = dyn_cast<ParenExpr>(e))
            return lower(p->getSubExpr(), allowMutation);
        if (auto const *c = dyn_cast<CastExpr>(e)) {
            auto from = lower(c->getSubExpr());
            auto to = scalar(c->getType());
            if (to == from.type)
                return from;
            if (to == "void")
                reject(e, "void casts are unsupported");
            return {K::Cast, to, {}, {std::move(from)}};
        }
        auto type = scalar(e->getType());
        if (auto const *n = dyn_cast<IntegerLiteral>(e))
            return {K::Atom,
                    type,
                    std::to_string(n->getValue().getZExtValue()) + (type == "uint" ? "u" : ""),
                    {}};
        if (auto const *n = dyn_cast<FloatingLiteral>(e)) {
            std::ostringstream out;
            out << std::scientific << std::setprecision(9) << n->getValueAsApproximateDouble();
            return {K::Atom, type, out.str(), {}};
        }
        if (auto const *b = dyn_cast<CXXBoolLiteralExpr>(e))
            return {K::Atom, type, b->getValue() ? "true" : "false", {}};
        if (auto const *d = dyn_cast<DeclRefExpr>(e)) {
            auto const *v = dyn_cast<VarDecl>(d->getDecl());
            if (!v || (!isa<ParmVarDecl>(v) && v->hasGlobalStorage()))
                reject(e, "global state is unsupported");
            return {K::Atom, type, name(d->getDecl()), {}};
        }
        if (auto const *m = dyn_cast<MemberExpr>(e)) {
            auto const *base = dyn_cast<DeclRefExpr>(m->getBase()->IgnoreParenImpCasts());
            if (base && base->getDecl() == invocation) {
                auto field = m->getMemberDecl()->getNameAsString();
                return {K::Atom, type, "gl_GlobalInvocationID." + field, {}};
            }
            if (base && base->getDecl() == params) {
                auto const *field = dyn_cast<FieldDecl>(m->getMemberDecl());
                for (std::size_t i = 0; i < fields.size(); ++i)
                    if (field == fields[i])
                        return {K::Atom, type, "p.f" + std::to_string(i), {}};
            }
            reject(e, "member access is limited to Invocation and Params");
        }
        if (auto const *u = dyn_cast<UnaryOperator>(e)) {
            if (u->getOpcode() == UO_AddrOf || u->getOpcode() == UO_Deref)
                reject(e, "pointer operations are unsupported");
            return {u->isPostfix() ? K::Postfix : K::Unary,
                    type,
                    UnaryOperator::getOpcodeStr(u->getOpcode()).str(),
                    {lower(u->getSubExpr())}};
        }
        if (auto const *b = dyn_cast<BinaryOperator>(e)) {
            if (b->getOpcode() == BO_Comma)
                reject(e, "comma expressions are unsupported");
            return {
                K::Binary, type, b->getOpcodeStr().str(), {lower(b->getLHS()), lower(b->getRHS())}};
        }
        if (auto const *c = dyn_cast<ConditionalOperator>(e))
            return {K::Conditional,
                    type,
                    {},
                    {lower(c->getCond()), lower(c->getTrueExpr()), lower(c->getFalseExpr())}};
        if (auto const *call = dyn_cast<CallExpr>(e))
            return lowerCall(call, type);
        reject(e, std::string{"unsupported expression: "} + e->getStmtClassName());
    }
    Expression lowerCall(CallExpr const *call, std::string const &type) {
        using K = Expression::Kind;
        if (auto const *member = dyn_cast<CXXMemberCallExpr>(call)) {
            auto const *object =
                dyn_cast<DeclRefExpr>(member->getImplicitObjectArgument()->IgnoreParenImpCasts());
            auto const *p = object ? dyn_cast<ParmVarDecl>(object->getDecl()) : nullptr;
            auto found = bindings.find(p);
            auto const *method = member->getMethodDecl();
            if (found == bindings.end() || !method ||
                method->getParent()->getCanonicalDecl() != record(p->getType()))
                reject(call, "unsupported member call");
            auto i = found->second;
            auto expected = writes[i] ? "store" : "load";
            if (method->getNameAsString() != expected ||
                call->getNumArgs() != (writes[i] ? 2u : 1u))
                reject(call, "unsupported buffer method");
            auto index = lower(call->getArg(0));
            Expression slot{
                K::Atom, "uint", "b" + std::to_string(i) + ".data[" + expression(index) + "]", {}};
            if (!writes[i])
                return slot;
            return {K::Binary, "void", "=", {std::move(slot), lower(call->getArg(1))}};
        }
        auto const *callee = call->getDirectCallee();
        if (!callee || isa<CXXMethodDecl>(callee))
            reject(call, "call must resolve to a free function");
        auto function = compile(callee, false);
        std::vector<Expression> args;
        for (auto const *arg : call->arguments())
            args.push_back(lower(arg));
        return {K::Call, type, function, std::move(args)};
    }
    Statement lower(Stmt const *s) {
        using K = Statement::Kind;
        if (!s)
            return {K::Empty, {}, {}, {}, {}};
        if (auto const *block = dyn_cast<CompoundStmt>(s)) {
            Statement result{K::Block, {}, {}, {}, {}};
            for (auto const *child : block->body())
                result.children.push_back(lower(child));
            return result;
        }
        if (auto const *decl = dyn_cast<DeclStmt>(s)) {
            if (!decl->isSingleDecl())
                reject(s, "declare one variable per statement");
            auto const *v = dyn_cast<VarDecl>(decl->getSingleDecl());
            if (!v || v->hasGlobalStorage() || !v->hasInit())
                reject(s, "locals require scalar initialization");
            return {K::Declaration, scalar(v->getType()), name(v), {lower(v->getInit())}, {}};
        }
        if (auto const *branch = dyn_cast<IfStmt>(s)) {
            if (branch->getInit() || branch->getConditionVariable())
                reject(s, "if initializers are unsupported");
            Statement result{K::If, {}, {}, {lower(branch->getCond())}, {lower(branch->getThen())}};
            if (branch->getElse())
                result.children.push_back(lower(branch->getElse()));
            return result;
        }
        if (auto const *loop = dyn_cast<ForStmt>(s)) {
            if (!loop->getCond() || !loop->getInc() || loop->getConditionVariable())
                reject(s, "for requires explicit condition and increment");
            return {K::For,
                    {},
                    {},
                    {lower(loop->getCond()), lower(loop->getInc(), true)},
                    {lower(loop->getInit()), lower(loop->getBody())}};
        }
        if (auto const *loop = dyn_cast<WhileStmt>(s)) {
            if (loop->getConditionVariable())
                reject(s, "while declaration is unsupported");
            return {K::While, {}, {}, {lower(loop->getCond())}, {lower(loop->getBody())}};
        }
        if (auto const *ret = dyn_cast<ReturnStmt>(s)) {
            Statement result{K::Return, {}, {}, {}, {}};
            if (ret->getRetValue())
                result.expressions.push_back(lower(ret->getRetValue()));
            return result;
        }
        if (isa<BreakStmt>(s))
            return {K::Break, {}, {}, {}, {}};
        if (isa<ContinueStmt>(s))
            return {K::Continue, {}, {}, {}, {}};
        if (isa<NullStmt>(s))
            return {K::Empty, {}, {}, {}, {}};
        if (auto const *e = dyn_cast<Expr>(s))
            return {K::Expression, {}, {}, {lower(e, true)}, {}};
        reject(s, std::string{"unsupported statement: "} + s->getStmtClassName());
    }
    std::string compile(FunctionDecl const *decl, bool isEntry) {
        auto const *definition = decl->getDefinition();
        if (!definition)
            rejectDecl(decl, "helper definition must be visible");
        auto const *key = definition->getCanonicalDecl();
        if (active.count(key))
            rejectDecl(decl, "recursive calls are unsupported");
        auto found = functions.find(key);
        if (found != functions.end())
            return found->second;
        if (definition->isVariadic() || definition->getDescribedFunctionTemplate() ||
            definition->isTemplated())
            rejectDecl(decl, "variadic and template functions are unsupported in "
                             "this compiler version");
        auto id = isEntry ? "main" : "fn" + std::to_string(functions.size());
        functions.emplace(key, id);
        active.insert(key);
        Function result{scalar(definition->getReturnType()), id, {}, {}};
        if (!isEntry) {
            for (auto const *p : definition->parameters())
                result.parameters.push_back({scalar(p->getType()), name(p)});
        }
        result.body = lower(definition->getBody());
        active.erase(key);
        ir.push_back(std::move(result));
        return id;
    }
    std::string shader() {
        signature();
        compile(entry, true);
        std::ostringstream out;
        out << "#version 450\nlayout(local_size_x=" << localSizeX
            << ",local_size_y=1,local_size_z=1) in;\n";
        for (std::size_t i = 0; i < writes.size(); ++i)
            out << "layout(set=0,binding=" << i << ",std430) "
                << (writes[i] ? "writeonly" : "readonly") << " buffer B" << i
                << " { uint data[]; } b" << i << ";\n";
        out << "layout(push_constant,std430) uniform Params {\n";
        for (std::size_t i = 0; i < fields.size(); ++i)
            out << "layout(offset=" << i * 4 << ") " << scalar(fields[i]->getType()) << " f" << i
                << ";\n";
        out << "} p;\n";
        for (auto const &f : ir) {
            out << f.type << ' ' << f.name << '(';
            for (std::size_t i = 0; i < f.parameters.size(); ++i)
                out << (i ? "," : "") << f.parameters[i].first << ' ' << f.parameters[i].second;
            out << ") " << statement(f.body) << '\n';
        }
        return out.str();
    }
    std::string header() {
        std::ostringstream out;
        out << "#pragma once\n#include <lutils/compute/Runtime.hpp>\nnamespace "
               "lutils::generated "
               "{\nstruct "
            << packageName << "Params {\n";
        for (auto const *field : fields) {
            auto type = scalar(field->getType());
            out << (type == "uint"  ? "std::uint32_t"
                    : type == "int" ? "std::int32_t"
                                    : "float")
                << ' ' << field->getNameAsString() << ";\n";
        }
        out << "};\nstd::vector<compute::Word> pack_" << packageName << "(" << packageName
            << "Params const &p);\n"
            << "compute::KernelSource " << packageName << "();\n}\n";
        return out.str();
    }
    std::string wrapper() {
        std::ostringstream out;
        out << "#include " << std::quoted(packageName.getValue() + ".hpp")
            << "\n#include <cstring>\n#include " << std::quoted(inputPath) << "\n";
        out << "#include " << std::quoted(packageName.getValue() + ".spv.hpp")
            << "\nnamespace lutils::generated {\n";
        auto paramType = paramRecord->getQualifiedNameAsString();
        out << "std::vector<compute::Word> pack_" << packageName << "(" << packageName
            << "Params const &p) {\nstd::vector<compute::Word> result(" << fields.size() << ");\n";
        for (std::size_t i = 0; i < fields.size(); ++i)
            out << "static_assert(sizeof(p." << fields[i]->getNameAsString()
                << ") == 4);\nstd::memcpy(&result[" << i << "], &p." << fields[i]->getNameAsString()
                << ",4);\n";
        out << "return result;\n}\ncompute::KernelSource " << packageName
            << "() {\ncompute::KernelSource result;\nresult.name="
            << std::quoted(entryName.getValue()) << ";\nresult.parameterWords=" << fields.size()
            << ";\nresult.bindings={";
        for (std::size_t i = 0; i < writes.size(); ++i)
            out << (i ? "," : "") << "compute::Access::" << (writes[i] ? "Write" : "Read");
        out << "};\nresult.localSize={" << localSizeX << ",1,1};\nresult.spirv=spirv_"
            << packageName
            << "();\nresult.cpu=+[](compute::kernel::Invocation id, "
               "std::vector<compute::CpuBuffer> const &b, "
               "std::vector<compute::Word> const &words) "
               "{\n"
            << paramType << " p{};\n";
        for (std::size_t i = 0; i < fields.size(); ++i)
            out << "std::memcpy(&p." << fields[i]->getNameAsString() << ",&words[" << i
                << "],4);\n";
        out << entryName << "(id";
        for (std::size_t i = 0; i < writes.size(); ++i)
            out << ",compute::kernel::" << (writes[i] ? "WriteBuffer" : "ReadBuffer") << "{b[" << i
                << "].data,b[" << i << "].size}";
        out << ",p);\n};\nreturn result;\n}\n}\n";
        return out.str();
    }
};
void write(std::string const &path, std::string const &text) {
    std::ofstream out{path};
    out << text;
    if (!out)
        throw std::runtime_error("cannot write " + path);
}
struct Consumer final : ASTConsumer, RecursiveASTVisitor<Consumer> {
    ASTContext *context = nullptr;
    FunctionDecl const *entry = nullptr;
    bool VisitFunctionDecl(FunctionDecl *decl) {
        if (decl->isThisDeclarationADefinition() && decl->getQualifiedNameAsString() == entryName) {
            if (entry)
                throw std::runtime_error("entry name is overloaded");
            entry = decl;
        }
        return true;
    }
    void HandleTranslationUnit(ASTContext &ctx) override {
        if (ctx.getDiagnostics().hasErrorOccurred()) {
            failed = true;
            return;
        }
        try {
            context = &ctx;
            TraverseDecl(ctx.getTranslationUnitDecl());
            if (!entry)
                throw std::runtime_error("entry not found: " + entryName.getValue());
            Compiler compiler{ctx, entry};
            auto glsl = compiler.shader();
            auto cpp = compiler.wrapper();
            write(outputBase + ".comp", glsl);
            write(outputBase + ".hpp", compiler.header());
            write(outputBase + ".cpp", cpp);
        } catch (std::exception const &e) {
            llvm::errs() << e.what() << '\n';
            failed = true;
        }
    }
};
struct Action final : ASTFrontendAction {
    bool BeginInvocation(CompilerInstance &ci) override {
        auto &opts = ci.getDependencyOutputOpts();
        llvm::SmallString<256> target;
        clang::quoteMakeTarget(outputBase.getValue() + ".hpp", target);
        opts.OutputFile = outputBase + ".d";
        opts.Targets = {target.str().str()};
        opts.IncludeSystemHeaders = false;
        return true;
    }
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
        return std::make_unique<Consumer>();
    }
};
int embed(char const *input, char const *output, char const *name) {
    checkOutput(input, output);
    std::ifstream in{input, std::ios::binary};
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{in}, {}};
    if (!in || bytes.empty() || bytes.size() % 4)
        throw std::runtime_error("invalid SPIR-V input");
    std::ostringstream text;
    text << "#pragma once\n#include <cstdint>\n#include <vector>\nnamespace "
            "lutils::generated "
            "{\ninline std::vector<std::uint32_t> spirv_"
         << name << "() { return {\n";
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        auto word = std::uint32_t{bytes[i]} | (std::uint32_t{bytes[i + 1]} << 8) |
                    (std::uint32_t{bytes[i + 2]} << 16) | (std::uint32_t{bytes[i + 3]} << 24);
        text << word << "u,";
        if (i % 32 == 28)
            text << '\n';
    }
    text << "}; }\n}\n";
    write(output, text.str());
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 5 && std::string{argv[1]} == "--embed")
            return embed(argv[2], argv[3], argv[4]);
        std::vector<char const *> arguments(argv, argv + argc);
        auto options =
            clang::tooling::CommonOptionsParser::create(argc, arguments.data(), category);
        if (!options) {
            llvm::errs() << options.takeError();
            return 1;
        }
        if (options->getSourcePathList().size() != 1)
            throw std::runtime_error("exactly one translation unit required");
        if (!localSizeX)
            throw std::runtime_error("local-size-x must be positive");
        inputPath = options->getSourcePathList().front();
        inputPath = std::filesystem::canonical(inputPath).string();
        for (auto suffix : {".cpp", ".hpp", ".comp", ".d"})
            checkOutput(inputPath, outputBase.getValue() + suffix);
        if (packageName.empty() || (packageName.front() >= '0' && packageName.front() <= '9') ||
            packageName.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN"
                                          "OPQRSTUVWXYZ0123456789_") != std::string::npos)
            throw std::runtime_error("package name must be a C++ identifier");
        clang::tooling::ClangTool tool{options->getCompilations(), options->getSourcePathList()};
        auto result = tool.run(clang::tooling::newFrontendActionFactory<Action>().get());
        return result || failed ? 1 : 0;
    } catch (std::exception const &e) {
        llvm::errs() << e.what() << '\n';
        return 1;
    }
}
