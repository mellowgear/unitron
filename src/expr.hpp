// asm900 expressions: numbers, symbols, `$`, char constants and the operators of TR p.12-16.
// Precedence (high to low): unary - ~ +, sizeof() startof(); * / %; + -; >> <<; &; ^; |; and the
// width operator `expr:n` (asm900 grammar rule 78): the value masked to n bits, encoded at least n
// bits wide. A parsed expression is a constant (folded) or a tree.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "util.hpp"

namespace unitron {

struct ExprError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// A symbol is not defined (yet).
struct Unknown : std::runtime_error {
    std::string name;
    explicit Unknown(const std::string& n) : std::runtime_error(n), name(n) {}
};

struct Expr;
using ExprP = std::shared_ptr<const Expr>;

struct Expr {
    enum Kind { Num, Sym, Dollar, Func, Neg, Not, Bin, Width } kind = Num;
    int64_t v = 0;       // Num: value; Width: n
    std::string name;    // Sym: name; Func: section name
    std::string op;      // Bin: operator; Func: "sizeof"/"startof"
    ExprP a, b;
    bool is_const() const { return kind == Num; }
};

ExprP num_expr(int64_t v);

// asm900's -H option: suffixed numbers (1234H, 101B, 17O/17Q, 99D) as well.
extern bool INTEL_SUFFIX;

// A parsed number: low 64 bits plus the exact decimal text when it does not fit (for messages).
struct Number {
    int64_t v = 0;
    bool big = false;
    std::string decimal;
};
Number parse_number_full(const std::string& t);   // throws ExprError
int64_t parse_number(const std::string& t);
int64_t wrap32(int64_t v);

Bytes unescape(const std::string& body);          // throws ExprError
int64_t char_value(const std::string& body);
int64_t apply_op(const std::string& op, int64_t a, int64_t b);

ExprP parse_expr(const std::string& text);         // throws ExprError

using VW = std::pair<int64_t, int>;               // value, width bits
using LookupFn = std::function<VW(const std::string&)>;
using HereFn = std::function<VW()>;
using FuncFn = std::function<VW(const std::string&, const std::string&)>;
VW evaluate(const ExprP& e, const LookupFn& lookup, const HereFn& here, const FuncFn* func = nullptr);

}  // namespace unitron
