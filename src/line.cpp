#include "line.hpp"

#include "regs.hpp"

namespace unitron {

ParsedInsn parse_insn(const std::string& mnem, const std::string& field) {
    ParsedInsn p;
    if (!split_mnemonic(mnem, p.base, p.sfx)) throw LineError("unknown mnemonic " + py_repr(mnem));
    std::vector<std::string> texts;
    if (!strip(field).empty()) texts = split_operands(field);
    size_t n = texts.size();
    try {
        for (size_t i = 0; i < n; i++) {
            bool cond = false;
            if (cond_first(p.base) && i == 0) {
                std::string u = upper(strip(texts[i]));
                if (p.base == "SCC" || (p.base == "RET" && n == 1) || (n >= 2 && cc_code(u) >= 0)) cond = true;
            }
            bool bare = bare_addr(p.base) && i == n - 1 && !(p.base == "LDA" && i == 0);
            p.ops.push_back(parse_operand(texts[i], bare, cond));
        }
    } catch (OperandError& e) {
        throw LineError(e.what());
    } catch (ExprError& e) {
        throw LineError(e.what());
    }
    return p;
}

static VW const_eval(const ExprP& e) {
    if (e->is_const()) return {e->v, 0};
    LookupFn nosym = [](const std::string& name) -> VW {
        throw LineError("symbol " + py_repr(name) + " in an expression (no symbol table here)");
    };
    HereFn here = []() -> VW { throw LineError("symbol '$' in an expression (no symbol table here)"); };
    return evaluate(e, nosym, here, nullptr);
}

Bytes assemble_line(const std::string& text, int64_t pc, const EvalFn* ev) {
    std::string t = strip(text);
    size_t k = t.find('\t');
    if (k == std::string::npos) k = t.find(' ');
    std::string mnem = k == std::string::npos ? t : t.substr(0, k);
    std::string field = k == std::string::npos ? "" : t.substr(k + 1);
    ParsedInsn p = parse_insn(strip(mnem), field);
    static const EvalFn ce = const_eval;
    try {
        return encode(p.base, p.sfx, p.ops, pc, ev ? *ev : ce, true);
    } catch (EncodeError& e) {
        throw LineError(e.what());
    }
}

bool assemble_or_none(const std::string& text, int64_t pc, Bytes& out) {
    try {
        out = assemble_line(text, pc);
        return true;
    } catch (LineError&) {
        return false;
    }
}

}  // namespace unitron
