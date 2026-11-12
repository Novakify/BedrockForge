// Renderer Foundation - IrisBridge custom uniforms (original code for Bedrock Forge / Renderer Foundation).
// Runs the bytecode Bedrock Forge's tools/IrisBridge/expr.py compiles from a shaderpack's shaders.properties
// (uniform.* / variable.* expressions). Mirrors expr.VM exactly; smooth() matches Iris (half-life = fade time / 10).
// Header-only so it can be unit-tested off Windows.
#pragma once
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace rf::iris {

enum class Op : uint8_t { Const, Input, Var, Neg, Not, Add, Sub, Mul, Div, Mod, Lt, Gt, Le, Ge, Eq, Ne, And, Or, Call, Smooth, Select };

struct Instr { Op op; double arg; };

struct UniformSlot {
    std::string name;    // "rainFactorUniform" or "foo.y"
    std::string uniform; // HLSL member name ("foo")
    int         comp = 0;
    bool        exported = true;
};

class UniformVM {
public:
    std::vector<std::string>        inputs;   // names the runtime fills each frame ("cameraPosition.x", "biome", ...)
    std::vector<UniformSlot>        slots;
    std::vector<std::vector<Instr>> code;     // one program per slot, run in order
    std::vector<std::string>        funcs;    // function table (index = CALL arg / 256)
    int                             smoothStates = 0;

    void reset() { vars_.assign(slots.size(), 0.0); sm_.assign(size_t(smoothStates), 0.0); smOn_.assign(size_t(smoothStates), 0); }
    int  inputIndex(std::string const& n) const {
        for (size_t i = 0; i < inputs.size(); ++i) if (inputs[i] == n) return int(i);
        return -1;
    }
    /// in: one value per `inputs` entry; dt = last frame time in seconds
    void run(std::vector<double> const& in, double dt) {
        if (vars_.size() != slots.size()) reset();
        std::vector<double> st;
        st.reserve(32);
        for (size_t s = 0; s < code.size(); ++s) {
            st.clear();
            for (Instr const& I : code[s]) {
                switch (I.op) {
                case Op::Const: st.push_back(I.arg); break;
                case Op::Input: { size_t k = size_t(I.arg); st.push_back(k < in.size() ? in[k] : 0.0); } break;
                case Op::Var: { size_t k = size_t(I.arg); st.push_back(k < vars_.size() ? vars_[k] : 0.0); } break;
                case Op::Neg: st.back() = -st.back(); break;
                case Op::Not: st.back() = st.back() != 0.0 ? 0.0 : 1.0; break;
                case Op::Select: { double e = pop(st), v = pop(st), c = pop(st); st.push_back(c != 0.0 ? v : e); } break;
                case Op::Smooth: {
                    double dn = pop(st), up = pop(st), x = pop(st);
                    size_t k = size_t(I.arg);
                    double cur = x;
                    if (k < sm_.size()) {
                        if (smOn_[k]) {
                            double T = x > sm_[k] ? up : dn;
                            cur = T <= 0.0 ? x : sm_[k] + (x - sm_[k]) * (1.0 - std::pow(2.0, -dt / (0.1 * T)));
                        }
                        sm_[k] = cur; smOn_[k] = 1;
                    }
                    st.push_back(cur);
                } break;
                case Op::Call: {
                    int const fid = int(I.arg) / 256, argc = int(I.arg) % 256;
                    double a[16]{};
                    int const n = argc < 16 ? argc : 16;
                    for (int i = n - 1; i >= 0; --i) a[i] = pop(st);
                    for (int i = n; i < argc; ++i) pop(st);
                    st.push_back(call(fid < int(funcs.size()) ? funcs[size_t(fid)] : std::string(), a, n));
                } break;
                default: {
                    double b = pop(st), a = pop(st), r = 0.0;
                    switch (I.op) {
                    case Op::Add: r = a + b; break;
                    case Op::Sub: r = a - b; break;
                    case Op::Mul: r = a * b; break;
                    case Op::Div: r = b != 0.0 ? a / b : 0.0; break;
                    case Op::Mod: r = b != 0.0 ? std::fmod(a, b) : 0.0; break;
                    case Op::Lt: r = a < b; break;
                    case Op::Gt: r = a > b; break;
                    case Op::Le: r = a <= b; break;
                    case Op::Ge: r = a >= b; break;
                    case Op::Eq: r = a == b; break;
                    case Op::Ne: r = a != b; break;
                    case Op::And: r = (a != 0.0) && (b != 0.0); break;
                    case Op::Or: r = (a != 0.0) || (b != 0.0); break;
                    default: break;
                    }
                    st.push_back(r);
                }
                }
            }
            vars_[s] = st.empty() ? 0.0 : st.back();
        }
    }
    double value(size_t slot) const { return slot < vars_.size() ? vars_[slot] : 0.0; }

private:
    std::vector<double>  vars_, sm_;
    std::vector<uint8_t> smOn_;
    std::mt19937         rng_{12345};

    static double pop(std::vector<double>& st) {
        if (st.empty()) return 0.0;
        double v = st.back(); st.pop_back(); return v;
    }
    double call(std::string const& f, double const* a, int n) {
        auto clamp = [](double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; };
        if (f == "sin") return std::sin(a[0]);
        if (f == "cos") return std::cos(a[0]);
        if (f == "tan") return std::tan(a[0]);
        if (f == "asin") return std::asin(clamp(a[0], -1, 1));
        if (f == "acos") return std::acos(clamp(a[0], -1, 1));
        if (f == "atan") return std::atan(a[0]);
        if (f == "atan2") return std::atan2(a[0], a[1]);
        if (f == "torad") return a[0] * 3.14159265358979323846 / 180.0;
        if (f == "todeg") return a[0] * 180.0 / 3.14159265358979323846;
        if (f == "abs") return std::fabs(a[0]);
        if (f == "floor") return std::floor(a[0]);
        if (f == "ceil") return std::ceil(a[0]);
        if (f == "round") return std::floor(a[0] + 0.5);
        if (f == "frac") return a[0] - std::floor(a[0]);
        if (f == "exp") return std::exp(a[0]);
        if (f == "exp2") return std::exp2(a[0]);
        if (f == "log") return a[0] > 0 ? std::log(a[0]) : -1e30;
        if (f == "log2") return a[0] > 0 ? std::log2(a[0]) : -1e30;
        if (f == "sqrt") return std::sqrt(a[0] > 0 ? a[0] : 0.0);
        if (f == "pow") return (a[0] >= 0 || a[1] == std::floor(a[1])) ? std::pow(a[0], a[1]) : 0.0;
        if (f == "signum") return double((a[0] > 0) - (a[0] < 0));
        if (f == "fmod") return a[1] != 0 ? std::fmod(a[0], a[1]) : 0.0;
        if (f == "min") { double m = a[0]; for (int i = 1; i < n; ++i) m = a[i] < m ? a[i] : m; return m; }
        if (f == "max") { double m = a[0]; for (int i = 1; i < n; ++i) m = a[i] > m ? a[i] : m; return m; }
        if (f == "clamp") return clamp(a[0], a[1], a[2]);
        if (f == "between") return (a[1] <= a[0] && a[0] <= a[2]) ? 1.0 : 0.0;
        if (f == "equals") return std::fabs(a[0] - a[1]) <= a[2] ? 1.0 : 0.0;
        if (f == "in") { for (int i = 1; i < n; ++i) if (a[0] == a[i]) return 1.0; return 0.0; }
        if (f == "random") return std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
        return 0.0;
    }
};

} // namespace rf::iris
