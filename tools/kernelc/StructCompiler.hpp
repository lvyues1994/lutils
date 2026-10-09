// Private Clang frontend for the C++17 shader object API.
struct StructCompiler {
    ASTContext &ctx;
    CXXRecordDecl const *entry;
    struct Type {
        std::string glsl, cpp;
        std::size_t bytes = 0, align = 1, count = 0;
        QualType element;
    };
    struct Resource {
        FieldDecl const *field;
        Type type;
        unsigned slot, dimensions = 0, format = 0;
        std::string qualifier, prefix;
        bool read = false, write = false;
    };
    struct UniformInfo {
        FieldDecl const *field;
        Type type;
        unsigned slot;
        std::size_t offset;
    };
    struct Record {
        CXXRecordDecl const *decl;
        Type type;
        std::vector<std::pair<FieldDecl const *, std::size_t>> fields;
    };
    std::map<clang::Type const *, Type> types;
    std::vector<Record> records;
    std::vector<Resource> resources;
    std::vector<UniformInfo> uniforms;
    std::vector<FieldDecl const *> constants;
    std::vector<std::pair<FieldDecl const *, Type>> shared;
    std::size_t sharedBytes = 0, sharedAlignment = 1;
    bool hasBarrier = false;
    bool usesHalf = false;
    std::map<ValueDecl const *, std::string> names;
    std::map<FunctionDecl const *, std::string> functions;
    std::set<FunctionDecl const *> active;
    std::vector<std::string> bodies;
    std::size_t parameterBytes = 16;
    unsigned local[3]{1, 1, 1}, next = 0, nextRecord = 0;
    std::string location;

    [[noreturn]] void fail(Decl const *d, std::string const &s) const {
        throw std::runtime_error(d->getLocation().printToString(ctx.getSourceManager()) + ": " + s);
    }
    [[noreturn]] void fail(Stmt const *e, std::string const &s) const {
        throw std::runtime_error(e->getBeginLoc().printToString(ctx.getSourceManager()) + ": " + s);
    }
    static std::size_t alignTo(std::size_t n, std::size_t a) { return (n + a - 1) / a * a; }
    ClassTemplateSpecializationDecl const *special(QualType t) const {
        return dyn_cast_or_null<ClassTemplateSpecializationDecl>(
            t.getNonReferenceType()->getAsCXXRecordDecl());
    }
    std::string templateName(QualType t) const {
        auto *s = special(t);
        return s ? s->getSpecializedTemplate()->getQualifiedNameAsString() : "";
    }
    std::string cppType(QualType t) const {
        PrintingPolicy p{ctx.getLangOpts()};
        p.SuppressTagKeyword = true;
        p.FullyQualifiedName = true;
        return t.getCanonicalType().getUnqualifiedType().getAsString(p);
    }
    Type type(QualType q) {
        if (q->isReferenceType() || q->isPointerType() || q.isVolatileQualified())
            fail(entry,
                 "shader values cannot be pointers, references or volatile: " + q.getAsString());
        auto t = q.getCanonicalType().getUnqualifiedType();
        auto it = types.find(t.getTypePtr());
        if (it != types.end())
            return it->second;
        Type out;
        out.cpp = cppType(t);
        if (t->isVoidType())
            out.glsl = "void";
        else if (t->isBooleanType()) {
            out.glsl = "bool";
            out.bytes = out.align = 4;
        } else if (t->isSpecificBuiltinType(BuiltinType::UInt)) {
            out.glsl = "uint";
            out.bytes = out.align = 4;
        } else if (t->isSpecificBuiltinType(BuiltinType::Int)) {
            out.glsl = "int";
            out.bytes = out.align = 4;
        } else if (t->isSpecificBuiltinType(BuiltinType::Float)) {
            out.glsl = "float";
            out.bytes = out.align = 4;
        } else if (t->isSpecificBuiltinType(BuiltinType::Double)) {
            out.glsl = "double";
            out.bytes = out.align = 8;
        } else if (auto *r = t->getAsCXXRecordDecl();
                   r && r->getQualifiedNameAsString() == "lutils::compute::kernel::half") {
            out.glsl = "float16_t";
            out.bytes = out.align = 2;
            usesHalf = true;
        } else if (auto *s = special(t)) {
            auto name = templateName(t);
            auto const &args = s->getTemplateArgs();
            if (name == "lutils::compute::kernel::Vec" ||
                name == "lutils::compute::kernel::Components") {
                auto base = type(args[0].getAsType());
                auto n = args[1].getAsIntegral().getZExtValue();
                auto prefix = base.glsl == "float"       ? ""
                              : base.glsl == "uint"      ? "u"
                              : base.glsl == "int"       ? "i"
                              : base.glsl == "double"    ? "d"
                              : base.glsl == "float16_t" ? "f16"
                                                         : "b";
                out.glsl = prefix + std::string{"vec"} + std::to_string(n);
                out.bytes = base.bytes * n;
                out.align = base.align * (n == 2 ? 2 : 4);
            } else if (name == "std::array") {
                auto base = type(args[0].getAsType());
                out.count = args[1].getAsIntegral().getZExtValue();
                if (!out.count || base.count ||
                    out.count >
                        UINT32_MAX / std::max<std::size_t>(1, alignTo(base.bytes, base.align)))
                    fail(entry,
                         "shader arrays require a positive bounded size and a non-array element");
                out.element = args[0].getAsType();
                out.glsl = base.glsl;
                out.align = base.align;
                out.bytes = alignTo(base.bytes, base.align) * out.count;
            } else
                fail(entry, "unsupported shader template type: " + t.getAsString());
        } else if (auto *r = t->getAsCXXRecordDecl()) {
            r = r->getDefinition();
            if (!r || !r->isAggregate() || !r->isTriviallyCopyable() || r->getNumBases() ||
                r->isUnion())
                fail(entry, "buffer structs must be trivial aggregates without bases");
            out.glsl = "S" + std::to_string(nextRecord++);
            Record record{r, out, {}};
            for (auto *f : r->fields()) {
                if (f->isBitField() || f->getAccess() != AS_public ||
                    f->getType().isConstQualified())
                    fail(f, "struct fields must be mutable public values");
                auto ft = type(f->getType());
                out.align = std::max(out.align, ft.align);
                out.bytes = alignTo(out.bytes, ft.align);
                record.fields.push_back({f, out.bytes});
                out.bytes += ft.bytes;
            }
            if (record.fields.empty())
                fail(r, "empty shader struct");
            out.bytes = alignTo(out.bytes, out.align);
            record.type = out;
            records.push_back(record);
        } else
            fail(entry, "unsupported shader type: " + t.getAsString());
        types.emplace(t.getTypePtr(), out);
        return out;
    }
    Expr const *strip(Expr const *e) const {
        while (true) {
            if (auto *p = dyn_cast<ParenExpr>(e))
                e = p->getSubExpr();
            else if (auto *p = dyn_cast<ExprWithCleanups>(e))
                e = p->getSubExpr();
            else if (auto *p = dyn_cast<MaterializeTemporaryExpr>(e))
                e = p->getSubExpr();
            else if (auto *p = dyn_cast<CXXBindTemporaryExpr>(e))
                e = p->getSubExpr();
            else if (auto *p = dyn_cast<CXXDefaultInitExpr>(e))
                e = p->getExpr();
            else if (auto *p = dyn_cast<CXXDefaultArgExpr>(e))
                e = p->getExpr();
            else
                return e;
        }
    }
    std::uint64_t integer(Expr const *e) const {
        Expr::EvalResult r;
        if (!e->EvaluateAsInt(r, ctx) || r.Val.getInt().isNegative())
            fail(e, "expected nonnegative constant integer");
        return r.Val.getInt().getZExtValue();
    }
    std::string name(ValueDecl const *d) {
        auto it = names.find(d);
        if (it != names.end())
            return it->second;
        return names.emplace(d, "v" + std::to_string(next++)).first->second;
    }
    void signature() {
        bool annotated = false;
        for (auto *a : entry->specific_attrs<AnnotateAttr>())
            annotated |= a->getAnnotation() == "kernel";
        if (!annotated)
            fail(entry, "kernel struct requires LUTILS_KERNEL or clang::annotate(\"kernel\")");
        if (entry->getNumBases() || entry->isUnion() || !entry->isAggregate() ||
            (entry->getDestructor() && entry->getDestructor()->isUserProvided()))
            fail(
                entry,
                "kernel structs must be aggregates without bases or user constructors/destructors");
        std::set<unsigned> bufferSlots, imageSlots, uniformSlots;
        for (auto *d : entry->decls()) {
            if (auto *v = dyn_cast<VarDecl>(d); v && v->getName() == "fileLocation") {
                auto *s = v->getInit()
                              ? dyn_cast<StringLiteral>(v->getInit()->IgnoreParenImpCasts())
                              : nullptr;
                if (!v->isConstexpr() || !s)
                    fail(v, "fileLocation must be a constexpr string");
                location = s->getString().str();
            }
        }
        if (location != packageName)
            fail(entry, "fileLocation must equal the CMake NAME (" + packageName.getValue() + ")");
        bool hasLocal = false;
        for (auto *f : entry->fields()) {
            if (f->getAccess() != AS_public)
                fail(f, "kernel fields must be public");
            if (f->getName() == "local_size") {
                hasLocal = true;
                auto *e = f->hasInClassInitializer() ? strip(f->getInClassInitializer()) : nullptr;
                auto *c = dyn_cast_or_null<CXXConstructExpr>(e);
                if (!c || c->getNumArgs() != 3 || type(f->getType()).glsl != "uvec3")
                    fail(f, "local_size requires uvec3{X,Y,Z}");
                for (unsigned i = 0; i < 3; ++i) {
                    auto n = integer(c->getArg(i));
                    if (!n || n > UINT32_MAX)
                        fail(f, "local size must be positive uint32");
                    local[i] = static_cast<unsigned>(n);
                }
                continue;
            }
            auto *s = special(f->getType());
            auto tn = templateName(f->getType());
            if (!s)
                fail(f, "kernel fields must be bindings, uniforms or constant arrays");
            auto const &args = s->getTemplateArgs();
            if (tn == "lutils::compute::kernel::BufferBinding") {
                auto slot = static_cast<unsigned>(args[1].getAsIntegral().getZExtValue());
                if (!bufferSlots.insert(slot).second)
                    fail(f, "duplicate buffer binding slot");
                auto element = type(args[0].getAsType());
                if (element.count)
                    fail(f, "buffer elements cannot be arrays; wrap the array in a struct");
                resources.push_back({f, element, slot});
                names[f] = "b" + std::to_string(resources.size() - 1) + ".data";
            } else if (tn == "lutils::compute::kernel::ImageBinding") {
                auto format = static_cast<unsigned>(args[0].getAsIntegral().getZExtValue());
                auto dim = static_cast<unsigned>(args[1].getAsIntegral().getZExtValue());
                auto slot = static_cast<unsigned>(args[3].getAsIntegral().getZExtValue());
                if (!imageSlots.insert(slot).second)
                    fail(f, "duplicate image binding slot");
                // Read format identity from the canonical enum declaration, not source spelling.
                auto *et = args[0].getIntegralType()->getAs<EnumType>();
                std::string formatName;
                for (auto *v : et->getDecl()->enumerators())
                    if (v->getInitVal().getZExtValue() == format)
                        formatName = v->getNameAsString();
                std::string qualifier;
                for (char ch : formatName)
                    qualifier += static_cast<char>(std::tolower(ch));
                // SDK names use UI/I and SNorm; GLSL uses ui/i and _snorm.
                auto pos = qualifier.find("snorm");
                if (pos != std::string::npos)
                    qualifier.replace(pos, 5, "_snorm");
                std::string prefix =
                    qualifier.back() == 'i'
                        ? (qualifier.size() > 1 && qualifier[qualifier.size() - 2] == 'u' ? "u"
                                                                                          : "i")
                        : "";
                Type imageType;
                imageType.cpp = formatName;
                resources.push_back({f, imageType, slot, dim, format, qualifier, prefix});
                names[f] = "b" + std::to_string(resources.size() - 1);
            } else if (tn == "lutils::compute::kernel::Uniform") {
                auto slot = static_cast<unsigned>(args[1].getAsIntegral().getZExtValue());
                if (!uniformSlots.insert(slot).second)
                    fail(f, "duplicate uniform location");
                auto value = type(args[0].getAsType());
                if (value.count || value.glsl == "void")
                    fail(f, "uniforms must be scalar, vector or value structs");
                parameterBytes = alignTo(parameterBytes, value.align);
                names[f] = "p.u" + std::to_string(uniforms.size());
                uniforms.push_back({f, value, slot, parameterBytes});
                parameterBytes += value.bytes;
            } else if (tn == "lutils::compute::kernel::SharedArray") {
                if (f->hasInClassInitializer())
                    fail(f, "shared arrays cannot have an initializer");
                auto element = type(args[0].getAsType());
                auto count = args[1].getAsIntegral().getZExtValue();
                if (!count || element.count ||
                    count > 65536 / alignTo(element.bytes, element.align))
                    fail(f, "shared arrays require a positive bounded size");
                element.count = count;
                element.bytes = alignTo(element.bytes, element.align) * count;
                sharedBytes = alignTo(sharedBytes, element.align) + element.bytes;
                sharedAlignment = std::max(sharedAlignment, element.align);
                names[f] = "shared_data.s" + std::to_string(shared.size());
                shared.push_back({f, element});
            } else if (tn == "std::array" && f->hasInClassInitializer()) {
                Expr::EvalResult constant;
                if (!f->getInClassInitializer()->EvaluateAsRValue(constant, ctx) ||
                    constant.HasSideEffects)
                    fail(f, "array member initializer must be constant");
                type(f->getType());
                names[f] = "c" + std::to_string(constants.size());
                constants.push_back(f);
            } else
                fail(f, "unsupported kernel member; constant arrays must be const std::array");
        }
        if (!hasLocal)
            fail(entry, "kernel requires local_size");
        if (parameterBytes > 128)
            fail(entry, "uniforms plus dispatch extent exceed 128 bytes");
    }
    void touch(Expr const *e, bool write) {
        e = strip(e)->IgnoreParenImpCasts();
        if (auto *m = dyn_cast<MemberExpr>(e)) {
            for (auto &r : resources)
                if (r.field == m->getMemberDecl()) {
                    if (write)
                        r.write = true;
                    else
                        r.read = true;
                    return;
                }
            touch(m->getBase(), write);
        } else if (auto *c = dyn_cast<CXXOperatorCallExpr>(e)) {
            if (c->getOperator() == OO_Subscript)
                touch(c->getArg(0), write);
        }
    }
    std::string args(CallExpr const *c, unsigned first = 0) {
        std::string s;
        for (unsigned i = first; i < c->getNumArgs(); ++i)
            s += (i == first ? "" : ",") + expr(c->getArg(i));
        return s;
    }
    std::string construct(QualType q, std::vector<Expr const *> const &values) {
        auto t = type(q);
        std::string s = t.glsl + (t.count ? "[" + std::to_string(t.count) + "]" : "") + "(";
        for (std::size_t i = 0; i < values.size(); ++i)
            s += (i ? "," : "") + expr(values[i]);
        if (values.empty()) {
            if (t.glsl[0] == 'S')
                fail(entry, "struct initialization must list its values");
            s += t.glsl == "bool" ? "false" : "0";
        }
        return s + ")";
    }
    std::size_t swizzleWidth(Expr const *e) const {
        e = strip(e)->IgnoreParenImpCasts();
        if (auto *call = dyn_cast<CXXOperatorCallExpr>(e);
            call && call->getOperator() == OO_Subscript) {
            auto *literal =
                dyn_cast<UserDefinedLiteral>(strip(call->getArg(1))->IgnoreParenImpCasts());
            if (literal)
                if (auto *text = dyn_cast<StringLiteral>(literal->getArg(0)->IgnoreParenImpCasts()))
                    return text->getLength();
        }
        fail(e, "swizzle conversion requires a literal subscript");
    }
    bool atomicCall(Stmt const *s) const {
        auto *e = dyn_cast<Expr>(s);
        auto *c = e ? dyn_cast<CallExpr>(strip(e)->IgnoreParenImpCasts()) : nullptr;
        auto *f = c ? c->getDirectCallee() : nullptr;
        if (!f || f->getQualifiedNameAsString().rfind("lutils::compute::kernel::", 0) != 0)
            return false;
        static std::set<std::string> names{"atomicAdd",      "atomicMin",     "atomicMax",
                                           "atomicAnd",      "atomicOr",      "atomicXor",
                                           "atomicExchange", "atomicCompSwap"};
        return names.count(f->getNameAsString()) != 0;
    }
    ValueDecl const *storageRoot(Expr const *e) const {
        e = strip(e)->IgnoreParenImpCasts();
        if (auto *m = dyn_cast<MemberExpr>(e)) {
            if (isa<CXXThisExpr>(m->getBase()->IgnoreParenImpCasts()))
                return m->getMemberDecl();
            return storageRoot(m->getBase());
        }
        if (auto *c = dyn_cast<CXXOperatorCallExpr>(e); c && c->getOperator() == OO_Subscript)
            return storageRoot(c->getArg(0));
        if (auto *a = dyn_cast<ArraySubscriptExpr>(e))
            return storageRoot(a->getBase());
        return nullptr;
    }
    bool containsReturn(Stmt const *s) const {
        if (isa<ReturnStmt>(s))
            return true;
        for (auto *child : s->children())
            if (child && containsReturn(child))
                return true;
        return false;
    }
    void validateBarriers(Stmt const *s, CompoundStmt const *body) {
        if (auto *c = dyn_cast<CallExpr>(s)) {
            auto *f = c->getDirectCallee();
            if (f && f->getQualifiedNameAsString() == "lutils::compute::kernel::barrier") {
                bool direct = false;
                if (body)
                    for (auto *statement : body->body())
                        if (auto *e = dyn_cast<Expr>(statement))
                            direct |= strip(e)->IgnoreParenImpCasts() == c;
                if (!direct)
                    fail(c, "barrier must be a top-level statement in main");
                hasBarrier = true;
            }
        }
        for (auto *child : s->children())
            if (child)
                validateBarriers(child, body);
    }
    bool mutates(Stmt const *s) const {
        if (auto *c = dyn_cast<CallExpr>(s); c && atomicCall(c))
            return true;
        if (auto *u = dyn_cast<UnaryOperator>(s); u && u->isIncrementDecrementOp())
            return true;
        if (auto *b = dyn_cast<BinaryOperator>(s); b && b->isAssignmentOp())
            return true;
        if (auto *c = dyn_cast<CXXOperatorCallExpr>(s)) {
            auto op = c->getOperator();
            if (op == OO_Equal || op == OO_PlusEqual || op == OO_MinusEqual || op == OO_StarEqual ||
                op == OO_SlashEqual || op == OO_PlusPlus || op == OO_MinusMinus)
                return true;
        }
        for (auto *c : s->children())
            if (c && mutates(c))
                return true;
        return false;
    }
    std::string expr(Expr const *original, bool write = false, bool allowMutation = false) {
        auto *e = strip(original);
        if (!allowMutation && mutates(e) && !atomicCall(e))
            fail(e, "nested mutation has no portable shader evaluation order");
        if (isa<CXXNewExpr>(e) || isa<CXXDeleteExpr>(e))
            fail(e, "dynamic allocation is unsupported");
        if (isa<CXXReinterpretCastExpr>(e))
            fail(e, "reinterpret casts are unsupported");
        if (auto *b = dyn_cast<BinaryOperator>(e);
            b && b->getType()->isIntegerType() && ctx.getTypeSize(b->getType()) > 32)
            fail(e, "64-bit integer expressions are unsupported");
        if (auto *c = dyn_cast<ImplicitCastExpr>(e)) {
            if (c->getCastKind() == CK_LValueToRValue || c->getCastKind() == CK_NoOp ||
                c->getCastKind() == CK_DerivedToBase ||
                c->getCastKind() == CK_UncheckedDerivedToBase ||
                c->getCastKind() == CK_UserDefinedConversion)
                return expr(c->getSubExpr(), write);
            if (c->getType()->isSpecificBuiltinType(BuiltinType::ULong) ||
                c->getType()->isSpecificBuiltinType(BuiltinType::ULongLong)) {
                // SDK indexing takes size_t. The shader's indices remain 32-bit.
                if (c->getCastKind() == CK_IntegralCast)
                    return "uint(" + expr(c->getSubExpr(), write) + ")";
            }
        }
        if (auto *c = dyn_cast<CastExpr>(e)) {
            if (c->getCastKind() == CK_ConstructorConversion || c->getCastKind() == CK_NoOp ||
                c->getCastKind() == CK_UserDefinedConversion)
                return expr(c->getSubExpr(), write);
            return type(c->getType()).glsl + "(" + expr(c->getSubExpr()) + ")";
        }
        if (auto *n = dyn_cast<IntegerLiteral>(e)) {
            if (ctx.getTypeSize(n->getType()) > 32)
                fail(e, "64-bit integer literals are unsupported");
            auto v = n->getValue().getZExtValue();
            if (v > UINT32_MAX)
                fail(e, "shader integer literal exceeds 32 bits");
            return std::to_string(v) + (n->getType()->isUnsignedIntegerType() ? "u" : "");
        }
        if (auto *n = dyn_cast<FloatingLiteral>(e)) {
            std::ostringstream out;
            out << std::scientific << std::setprecision(17) << n->getValueAsApproximateDouble();
            return out.str() +
                   (n->getType()->isSpecificBuiltinType(BuiltinType::Double) ? "lf" : "");
        }
        if (auto *b = dyn_cast<CXXBoolLiteralExpr>(e))
            return b->getValue() ? "true" : "false";
        if (auto *d = dyn_cast<DeclRefExpr>(e)) {
            if (auto *v = dyn_cast<VarDecl>(d->getDecl()); v && v->hasGlobalStorage()) {
                auto q = v->getQualifiedNameAsString();
                if (q.rfind("lutils::compute::kernel::gl_", 0) == 0) {
                    if (write)
                        fail(e, "shader builtins are read only");
                    return v->getNameAsString();
                }
                if (v->isConstexpr() && v->hasInit())
                    return expr(v->getInit());
                fail(e, "global state is unsupported");
            }
            return name(d->getDecl());
        }
        if (auto *m = dyn_cast<MemberExpr>(e)) {
            if (isa<CXXThisExpr>(m->getBase()->IgnoreParenImpCasts())) {
                auto it = names.find(m->getMemberDecl());
                if (it == names.end())
                    fail(e, "unsupported kernel member access");
                for (auto const &u : uniforms)
                    if (u.field == m->getMemberDecl() && write)
                        fail(e, "uniforms are read only");
                for (auto *f : constants)
                    if (f == m->getMemberDecl() && write)
                        fail(e, "kernel constant arrays are read only");
                touch(e, write);
                return it->second;
            }
            return "(" + expr(m->getBase(), write) + ")." + m->getMemberDecl()->getNameAsString();
        }
        if (auto *c = dyn_cast<CXXConstructExpr>(e)) {
            if (c->getNumArgs() == 1 &&
                templateName(c->getArg(0)->getType()) == "lutils::compute::kernel::SwizzleValue") {
                auto *vector = special(c->getType());
                if (!vector || templateName(c->getType()) != "lutils::compute::kernel::Vec" ||
                    vector->getTemplateArgs()[1].getAsIntegral().getZExtValue() !=
                        swizzleWidth(c->getArg(0)))
                    fail(e, "swizzle component count does not match destination vector");
            }
            if (c->getConstructor()->isCopyOrMoveConstructor())
                return expr(c->getArg(0), write);
            return construct(c->getType(), {c->arg_begin(), c->arg_end()});
        }
        if (auto *l = dyn_cast<InitListExpr>(e)) {
            if (templateName(l->getType()) == "std::array" && l->getNumInits() == 1) {
                if (auto *a = dyn_cast<InitListExpr>(l->getInit(0)))
                    return construct(l->getType(), {a->inits().begin(), a->inits().end()});
            }
            return construct(l->getType(), {l->inits().begin(), l->inits().end()});
        }
        if (auto *v = dyn_cast<ImplicitValueInitExpr>(e))
            return construct(v->getType(), {});
        if (auto *u = dyn_cast<UnaryOperator>(e)) {
            if (u->getOpcode() == UO_AddrOf || u->getOpcode() == UO_Deref)
                fail(e, "pointer operations are unsupported");
            if (u->isIncrementDecrementOp())
                touch(u->getSubExpr(), false);
            auto arg = expr(u->getSubExpr(), u->isIncrementDecrementOp());
            auto op = UnaryOperator::getOpcodeStr(u->getOpcode()).str();
            return "(" + (u->isPostfix() ? arg + op : op + arg) + ")";
        }
        if (auto *b = dyn_cast<BinaryOperator>(e)) {
            if (b->getOpcode() == BO_Comma)
                fail(e, "comma expressions are unsupported");
            if (b->isCompoundAssignmentOp())
                touch(b->getLHS(), false);
            return "(" + expr(b->getLHS(), b->isAssignmentOp()) + " " + b->getOpcodeStr().str() +
                   " " + expr(b->getRHS()) + ")";
        }
        if (auto *c = dyn_cast<ConditionalOperator>(e))
            return "(" + expr(c->getCond()) + "?" + expr(c->getTrueExpr()) + ":" +
                   expr(c->getFalseExpr()) + ")";
        if (auto *a = dyn_cast<ArraySubscriptExpr>(e))
            return expr(a->getBase(), write) + "[" + expr(a->getIdx()) + "]";
        if (auto *c = dyn_cast<CXXOperatorCallExpr>(e)) {
            auto *callee = c->getDirectCallee();
            if (!callee ||
                (callee->getQualifiedNameAsString().rfind("lutils::compute::kernel::", 0) != 0 &&
                 callee->getQualifiedNameAsString().rfind("std::array", 0) != 0 &&
                 !callee->isImplicit()))
                fail(e, "custom overloaded operators are unsupported in shaders");
            auto op = c->getOperator();
            if (op == OO_Subscript) {
                auto *index = strip(c->getArg(1))->IgnoreParenImpCasts();
                if (auto *sw = dyn_cast<UserDefinedLiteral>(index)) {
                    if (sw->getDirectCallee()->getNameAsString() != "operator\"\"_sw")
                        fail(e, "unsupported subscript literal");
                    auto *literal = dyn_cast<StringLiteral>(sw->getArg(0)->IgnoreParenImpCasts());
                    if (!literal)
                        fail(e, "swizzle requires a string literal");
                    auto text = literal->getString().str();
                    auto bt = type(c->getArg(0)->getType().getNonReferenceType());
                    unsigned n = static_cast<unsigned>(bt.glsl.back() - '0');
                    if (text.size() < 1 || text.size() > 4)
                        fail(e, "swizzles have one to four components");
                    for (char ch : text) {
                        auto p = std::string{"xyzw"}.find(ch);
                        if (p == std::string::npos)
                            p = std::string{"rgba"}.find(ch);
                        if (p == std::string::npos)
                            p = std::string{"stpq"}.find(ch);
                        if (p >= n)
                            fail(e, "swizzle component outside vector");
                    }
                    if (write)
                        fail(e, "swizzles are value reads");
                    return "(" + expr(c->getArg(0)) + ")." + text;
                }
                return expr(c->getArg(0), write) + "[" + expr(c->getArg(1)) + "]";
            }
            auto token = std::string{getOperatorSpelling(op)};
            bool assignment = op == OO_Equal || op == OO_PlusEqual || op == OO_MinusEqual ||
                              op == OO_StarEqual || op == OO_SlashEqual;
            if (assignment && op != OO_Equal)
                touch(c->getArg(0), false);
            if (c->getNumArgs() == 1)
                return "(" + token + expr(c->getArg(0)) + ")";
            if (c->getNumArgs() == 2)
                return "(" + expr(c->getArg(0), assignment) + " " + token + " " +
                       expr(c->getArg(1)) + ")";
            fail(e, "unsupported operator");
        }
        if (auto *c = dyn_cast<CXXMemberCallExpr>(e)) {
            auto *m = c->getMethodDecl();
            auto *object = c->getImplicitObjectArgument();
            if (isa<CXXConversionDecl>(m) &&
                m->getParent()->getQualifiedNameAsString() == "lutils::compute::kernel::half")
                return type(c->getType()).glsl + "(" + expr(object) + ")";
            if (isa<CXXConversionDecl>(m) &&
                templateName(object->getType()) == "lutils::compute::kernel::SwizzleValue") {
                if (swizzleWidth(object) != 1)
                    fail(e, "scalar swizzle requires one component");
                return expr(object);
            }
            if (isa<CXXConversionDecl>(m) &&
                templateName(object->getType()) == "lutils::compute::kernel::Uniform")
                return expr(object);
            if (m->getNameAsString() == "size" && templateName(object->getType()) == "std::array") {
                if (object->HasSideEffects(ctx))
                    fail(e, "array size() cannot discard object side effects");
                return std::to_string(type(object->getType()).count) + "u";
            }
            if (m->getParent()->getCanonicalDecl() == entry->getCanonicalDecl()) {
                if (!m->isStatic() && !isa<CXXThisExpr>(strip(object)->IgnoreParenImpCasts()))
                    fail(e, "kernel helper receiver must be this");
                return function(m, false) + "(" + args(c) + ")";
            }
            fail(e, "unsupported shader member call");
        }
        if (auto *c = dyn_cast<CallExpr>(e)) {
            auto *f = c->getDirectCallee();
            if (!f)
                fail(e, "indirect calls are unsupported");
            auto q = f->getQualifiedNameAsString(), n = f->getNameAsString();
            if (q.rfind("lutils::compute::kernel::", 0) == 0) {
                static std::set<std::string> intrinsic{
                    "dot",   "cross",     "length",     "normalize", "reflect", "min",  "max",
                    "clamp", "sqrt",      "abs",        "pow",       "floor",   "ceil", "sin",
                    "cos",   "imageLoad", "imageStore", "imageSize", "barrier"};
                if (atomicCall(c)) {
                    for (auto *argument : c->arguments())
                        if (mutates(argument))
                            fail(argument, "nested mutation in atomic arguments is unsupported");
                    auto *root = storageRoot(c->getArg(0));
                    bool valid = false;
                    for (auto &r : resources)
                        if (r.field == root && !r.dimensions) {
                            valid = true;
                            r.read = r.write = true;
                        }
                    for (auto const &v : shared)
                        valid |= v.first == root;
                    auto value = type(c->getArg(0)->getType());
                    if (!valid || (value.glsl != "int" && value.glsl != "uint"))
                        fail(e, "atomics require int32/uint32 buffer or shared lvalues");
                    return n + "(" + expr(c->getArg(0), true) + "," + args(c, 1) + ")";
                }
                if (!intrinsic.count(n))
                    fail(e, "unsupported shader intrinsic: " + n);
                if (n == "imageStore") {
                    touch(c->getArg(0), true);
                    // Resource arguments themselves are descriptors, not image reads.
                    auto image = expr(c->getArg(0), true);
                    return n + "(" + image + "," + args(c, 1) + ")";
                }
                return n + "(" + args(c) + ")";
            }
            return function(f, false) + "(" + args(c) + ")";
        }
        fail(e, std::string{"unsupported shader expression: "} + e->getStmtClassName());
    }
    std::string declaration(Type const &t, std::string const &n) const {
        return t.glsl + " " + n + (t.count ? "[" + std::to_string(t.count) + "]" : "");
    }
    std::string stmt(Stmt const *s) {
        if (!s)
            return ";";
        if (auto *b = dyn_cast<CompoundStmt>(s)) {
            std::string out = "{\n";
            for (auto *v : b->body())
                out += stmt(v) + "\n";
            return out + "}";
        }
        if (auto *d = dyn_cast<DeclStmt>(s)) {
            std::string out;
            for (auto *decl : d->decls()) {
                auto *v = dyn_cast<VarDecl>(decl);
                if (!v || v->hasGlobalStorage() || !v->hasInit())
                    fail(s, "shader locals require initialized values");
                // Diagnose the initializer before its deduced type, consistently
                // across host compilers' string-concatenation evaluation orders.
                auto initial = expr(v->getInit());
                auto valueType = type(v->getType());
                out += declaration(valueType, name(v)) + " = " + initial + ";\n";
            }
            return out;
        }
        if (auto *b = dyn_cast<IfStmt>(s)) {
            if (b->getInit() || b->getConditionVariable())
                fail(s, "if initializers are unsupported");
            return "if(" + expr(b->getCond()) + ")" + stmt(b->getThen()) +
                   (b->getElse() ? "else " + stmt(b->getElse()) : "");
        }
        if (auto *f = dyn_cast<ForStmt>(s)) {
            if (!f->getCond() || !f->getInc() || f->getConditionVariable())
                fail(s, "for requires condition and increment");
            return "for(" + stmt(f->getInit()) + expr(f->getCond()) + ";" +
                   expr(f->getInc(), false, true) + ")" + stmt(f->getBody());
        }
        if (auto *w = dyn_cast<WhileStmt>(s))
            return "while(" + expr(w->getCond()) + ")" + stmt(w->getBody());
        if (auto *r = dyn_cast<ReturnStmt>(s))
            return std::string{"return"} + (r->getRetValue() ? " " + expr(r->getRetValue()) : "") +
                   ";";
        if (isa<BreakStmt>(s))
            return "break;";
        if (isa<ContinueStmt>(s))
            return "continue;";
        if (isa<NullStmt>(s))
            return ";";
        if (auto *e = dyn_cast<Expr>(s))
            return expr(e, false, true) + ";";
        fail(s, std::string{"unsupported shader statement: "} + s->getStmtClassName());
    }
    std::string function(FunctionDecl const *f, bool main) {
        auto *d = f->getDefinition();
        if (!d)
            fail(f, "shader helper definition must be visible");
        auto *key = d->getCanonicalDecl();
        if (active.count(key))
            fail(f, "recursive shader helpers are unsupported");
        auto it = functions.find(key);
        if (it != functions.end())
            return it->second;
        if (d->isVariadic())
            fail(f, "variadic helpers are unsupported");
        validateBarriers(d->getBody(), main ? dyn_cast<CompoundStmt>(d->getBody()) : nullptr);
        if (main && hasBarrier && containsReturn(d->getBody()))
            fail(d, "barrier kernels cannot return early");
        std::string n = main ? "kernel_main" : "fn" + std::to_string(functions.size());
        functions.emplace(key, n);
        active.insert(key);
        auto ret = type(d->getReturnType());
        if (ret.count)
            fail(d, "array return values are unsupported");
        std::string out = ret.glsl + " " + n + "(";
        for (unsigned i = 0; i < d->getNumParams(); ++i) {
            auto *p = d->getParamDecl(i);
            out += (i ? "," : "") + declaration(type(p->getType()), name(p));
        }
        out += ")" + stmt(d->getBody());
        active.erase(key);
        bodies.push_back(out);
        return n;
    }
    std::string shader() {
        signature();
        CXXMethodDecl const *main = nullptr;
        for (auto *m : entry->methods())
            if (m->getNameAsString() == "main") {
                if (main || m->getNumParams() || !m->getReturnType()->isVoidType() || m->isStatic())
                    fail(m, "kernel requires one void main() method");
                main = m;
            }
        if (!main)
            fail(entry, "kernel main() not found");
        function(main, true);
        std::vector<std::string> initializers;
        for (auto *f : constants)
            initializers.push_back(expr(f->getInClassInitializer()));
        std::ostringstream out;
        out << "#version 450\n";
        if (usesHalf)
            out << "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n"
                   "#extension GL_EXT_shader_16bit_storage : require\n";
        out << "layout(local_size_x=" << local[0] << ",local_size_y=" << local[1]
            << ",local_size_z=" << local[2] << ") in;\n";
        for (auto const &r : records) {
            out << "struct " << r.type.glsl << " {\n";
            for (auto const &f : r.fields)
                out << declaration(type(f.first->getType()), f.first->getNameAsString()) << ";\n";
            out << "};\n";
        }
        if (!shared.empty()) {
            out << "struct SharedData {\n";
            for (std::size_t i = 0; i < shared.size(); ++i)
                out << declaration(shared[i].second, "s" + std::to_string(i)) << ";\n";
            out << "};\nshared SharedData shared_data;\n";
        }
        for (std::size_t i = 0; i < resources.size(); ++i) {
            auto const &r = resources[i];
            out << "layout(set=0,binding=" << i;
            auto access = r.write ? (r.read ? "" : "writeonly ") : "readonly ";
            if (r.dimensions)
                out << "," << r.qualifier << ") uniform " << access << r.prefix << "image"
                    << r.dimensions << "D b" << i << ";\n";
            else
                out << ",std430) " << access << "buffer B" << i << " { " << r.type.glsl
                    << " data[]; } b" << i << ";\n";
        }
        out << "layout(push_constant,std430) uniform Params { layout(offset=0) uvec4 extent;\n";
        for (auto const &u : uniforms)
            out << "layout(offset=" << u.offset << ") "
                << declaration(u.type, names[u.field].substr(2)) << ";\n";
        out << "} p;\n";
        for (std::size_t i = 0; i < constants.size(); ++i)
            out << "const " << declaration(type(constants[i]->getType()), name(constants[i]))
                << " = " << initializers[i] << ";\n";
        for (auto const &b : bodies)
            out << b << "\n";
        out << "void main() { ";
        if (shared.empty() && !hasBarrier)
            out << "if(any(greaterThanEqual(gl_GlobalInvocationID,p.extent.xyz))) return; ";
        out << "kernel_main(); }\n";
        return out.str();
    }
    std::string qualified() const { return "::" + entry->getQualifiedNameAsString(); }
    std::string macroChecks() const {
        std::map<std::string, std::pair<bool, std::string>> profile;
        for (auto const &m : preprocessingMacros) {
            auto split = m.first.find('=');
            auto name = m.first.substr(0, split);
            if (name.find('(') != std::string::npos)
                fail(entry, "shader command-line macros must be object-like");
            profile[name] = {m.second,
                             split == std::string::npos ? "1" : m.first.substr(split + 1)};
        }
        std::ostringstream o;
        o << "#include <string_view>\n#define LUTILS_SHADER_TEXT_IMPL(...) #__VA_ARGS__\n#define "
             "LUTILS_SHADER_TEXT(...) LUTILS_SHADER_TEXT_IMPL(__VA_ARGS__)\n";
        for (auto const &m : profile) {
            auto const &n = m.first;
            if (m.second.first)
                o << "#ifdef " << n << "\n#error shader macro must be undefined: " << n
                  << "\n#endif\n";
            else
                o << "#ifndef " << n << "\n#error shader macro missing: " << n
                  << "\n#else\nstatic_assert(std::string_view{LUTILS_SHADER_TEXT(" << n
                  << ")} == LUTILS_SHADER_TEXT(" << m.second.second
                  << "), \"shader macro mismatch: " << n << "\");\n#endif\n";
        }
        o << "#undef LUTILS_SHADER_TEXT\n#undef LUTILS_SHADER_TEXT_IMPL\n";
        return o.str();
    }
    std::string header() {
        std::ostringstream o;
        o << "#pragma once\n"
          << macroChecks() << "#include <lutils/compute/Compute.hpp>\n#include "
          << std::quoted(inputPath) << "\nnamespace lutils::compute {\n";
        for (auto const &r : records) {
            auto guard = "LUTILS_CODEC_" + r.decl->getQualifiedNameAsString();
            for (char &c : guard)
                if (!std::isalnum(static_cast<unsigned char>(c)))
                    c = '_';
            o << "#ifndef " << guard << "\n#define " << guard << "\ntemplate<> struct StorageCodec<"
              << r.type.cpp << "> {\nstatic constexpr std::size_t bytes=" << r.type.bytes
              << ", alignmentBytes=" << r.type.align
              << ";\nstatic constexpr std::size_t words=(bytes+3)/4, "
                 "alignment=(alignmentBytes+3)/4;\nstatic "
              << r.type.cpp << " read(void const *data) { auto const *p=storageBytes(data); "
              << r.type.cpp << " v{};\n";
            for (auto const &f : r.fields)
                o << "v." << f.first->getNameAsString() << "=StorageCodec<"
                  << cppType(f.first->getType()) << ">::read(p+" << f.second << ");\n";
            o << "return v; }\nstatic void write(void *data," << r.type.cpp
              << " const &v) { auto *p=storageBytes(data);\n";
            for (auto const &f : r.fields)
                o << "StorageCodec<" << cppType(f.first->getType()) << ">::write(p+" << f.second
                  << ",v." << f.first->getNameAsString() << ");\n";
            o << "}\n};\n#endif\n";
        }
        o << "template<> struct KernelTraits<" << qualified()
          << "> {\nstatic KernelSource source();\nstatic std::vector<Word> pack(" << qualified()
          << " const &,Extent3);\nstatic Result<void> record(Backend &,CommandList &,KernelHandle,"
          << qualified() << " const &,Extent3);\n};\n}\n";
        return o.str();
    }
    std::string wrapper() {
        std::ostringstream o;
        o << "#include " << std::quoted(packageName.getValue() + ".hpp") << "\n#include "
          << std::quoted(packageName.getValue() + ".spv.hpp")
          << "\nnamespace lutils::compute {\nKernelSource KernelTraits<" << qualified()
          << ">::source() { KernelSource s; s.abiVersion=2; s.name=" << std::quoted(location)
          << "; s.localSize={" << local[0] << "," << local[1] << "," << local[2]
          << "}; s.parameterWords=" << (parameterBytes + 3) / 4
          << ";\ns.spirv=lutils::generated::spirv_" << packageName << "();\n";
        for (auto const &r : resources) {
            o << "s.bindings.push_back(Access::"
              << (r.write ? (r.read ? "ReadWrite" : "Write") : "Read")
              << ");\ns.resources.push_back(BindingInfo{" << r.slot << ",";
            if (r.dimensions)
                o << "1,ImageDesc{kernel::ImageFormat::" << r.type.cpp << "," << r.dimensions
                  << ",{1,1,1}}";
            else
                o << (alignTo(r.type.bytes, r.type.align) + 3) / 4 << ",{},"
                  << alignTo(r.type.bytes, r.type.align);
            o << "});\n";
        }
        o << "s.sharedMemoryBytes=" << alignTo(sharedBytes, sharedAlignment)
          << "; s.requiresFullWorkgroups=" << (!shared.empty() || hasBarrier ? "true" : "false")
          << ";\n";
        o << "s.cpuDispatch=+[](Extent3 extent,std::vector<CpuBuffer> const &b,std::vector<Word> "
             "const &p,CpuExecution &execution) { "
          << qualified() << " k{}; (void)p;\n";
        for (std::size_t i = 0; i < resources.size(); ++i) {
            auto const &r = resources[i];
            auto n = r.field->getNameAsString();
            if (r.dimensions)
                o << "k." << n << ".cpuView(b[" << i << "].data,imageExtent<kernel::Dim::D"
                  << r.dimensions << ">(*b[" << i << "].image));\n";
            else
                o << "CpuStorage<" << r.type.cpp << "> v" << i << "{b[" << i << "]}; k." << n
                  << ".cpuView(v" << i << ".values.data(),v" << i << ".values.size());\n";
        }
        for (auto const &u : uniforms)
            o << "k." << u.field->getNameAsString() << "=StorageCodec<" << u.type.cpp
              << ">::read(storageBytes(p.data())+" << u.offset << ");\n";
        for (std::size_t i = 0; i < shared.size(); ++i) {
            auto const &v = shared[i];
            o << "std::array<" << v.second.cpp << "," << v.second.count << "> shared" << i << "; k."
              << v.first->getNameAsString() << ".cpuView(shared" << i << ".data());\n";
        }
        o << "executeCpu<" << local[0] << "," << local[1] << "," << local[2]
          << ">(k,extent,execution," << (!shared.empty() || hasBarrier ? "true" : "false")
          << ");\n";
        for (std::size_t i = 0; i < resources.size(); ++i)
            if (!resources[i].dimensions && resources[i].write)
                o << "v" << i << ".flush();\n";
        o << "}; return s; }\nResult<void> KernelTraits<" << qualified()
          << ">::record(Backend &backend,CommandList &commands,KernelHandle handle," << qualified()
          << " const &k,Extent3 extent) {\n";
        o << "if(k.local_size != kernel::uvec3{" << local[0] << "," << local[1] << "," << local[2]
          << "}) return Error{ErrorCode::InvalidArgument,\"local_size differs from compiled "
             "kernel\"};\n";
        for (auto *f : constants)
            o << "if(k." << f->getNameAsString() << " != " << qualified() << "{}."
              << f->getNameAsString()
              << ") return Error{ErrorCode::InvalidArgument,\"constant array differs from compiled "
                 "kernel\"};\n";
        o << "std::vector<BufferHandle> buffers;\n";
        for (std::size_t i = 0; i < resources.size(); ++i)
            o << "auto b" << i << "=backend.resolve(k." << resources[i].field->getNameAsString()
              << "); if(!b" << i << ") return b" << i << ".error(); buffers.push_back(b" << i
              << ".value());\n";
        o << "return "
             "commands.dispatch(Dispatch{std::move(handle),std::move(buffers),pack(k,extent),"
             "extent});\n}\n";
        o << "std::vector<Word> KernelTraits<" << qualified() << ">::pack(" << qualified()
          << " const &k,Extent3 extent) { (void)k;\n";
        o << "std::vector<Word> p(" << (parameterBytes + 3) / 4
          << ",0); p[0]=extent.x;p[1]=extent.y;p[2]=extent.z;\n";
        for (auto const &u : uniforms)
            o << "StorageCodec<" << u.type.cpp << ">::write(storageBytes(p.data())+" << u.offset
              << ",k." << u.field->getNameAsString() << ".value);\n";
        o << "return p;\n}\n}\n";
        return o.str();
    }
};
