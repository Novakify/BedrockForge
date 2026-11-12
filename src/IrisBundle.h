// Renderer Foundation - IrisBridge bundle loader (original code). Reads <bundle>/runtime.txt written by Bedrock Forge's
// tools/IrisBridge/bundle.py: pipeline constants, the custom-uniform program and every pass with its DXIL, $Globals
// layout, resources and render targets. Header-only (no Windows types) so it is unit-tested off Windows.
#pragma once
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "IrisUniformVM.h"

namespace rf::iris {

struct GlobalVar { std::string name, type; unsigned offset = 0; };
struct Resource  { std::string name, kind, reg; unsigned slot = 0; };
struct VsInput   { std::string sem; unsigned index = 0; std::string mask, fmt; };
struct Stage {
    std::string                        dxil;        // relative to the bundle folder
    unsigned                           globalsSize = 0;
    std::vector<GlobalVar>             globals;
    std::vector<Resource>              resources;
    std::vector<VsInput>               inputs;      // vertex stage only
    std::map<std::string, std::string> defaults;    // GLSL uniform initialisers (lost in HLSL)
};
struct Pass {
    std::string                        world, program, kind, source;
    std::vector<int>                   targets;     // colortex index per SV_Target
    std::map<std::string, std::string> blend;       // "*" or "colortexN" -> mode
    std::string                        alphaTest;
    std::map<std::string, Stage>       stages;      // "vert" / "frag" / "comp"
};
struct Bundle {
    std::string                        pack;
    std::map<std::string, std::string> consts;
    UniformVM                          vm;
    std::vector<Pass>                  passes;
    std::string                        error;

    std::string constOr(std::string const& k, std::string const& d) const {
        auto it = consts.find(k); return it == consts.end() ? d : it->second;
    }
    std::vector<Pass const*> chain(std::string const& world, std::string const& kind) const {
        std::vector<Pass const*> v;
        for (auto const& p : passes) if (p.world == world && p.kind == kind) v.push_back(&p);
        return v;
    }
    bool load(std::string const& file) {
        std::ifstream in(file);
        if (!in) { error = "can't open " + file; return false; }
        std::string line, tag;
        Pass*  pass  = nullptr;
        Stage* stage = nullptr;
        int    lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            std::istringstream s(line);
            if (!(s >> tag)) continue;
            if (tag == "irisbridge") { int v = 0; s >> v; if (v != 1) { error = "bundle format " + std::to_string(v); return false; } }
            else if (tag == "pack") std::getline(s >> std::ws, pack);
            else if (tag == "const") { std::string k, v; s >> k; std::getline(s >> std::ws, v); consts[k] = v; }
            else if (tag == "funcs") { std::string f; while (s >> f) vm.funcs.push_back(f); }
            else if (tag == "smooth") s >> vm.smoothStates;
            else if (tag == "uinput") { size_t i; std::string n; s >> i >> n; if (vm.inputs.size() <= i) vm.inputs.resize(i + 1); vm.inputs[i] = n; }
            else if (tag == "uslot") {
                size_t i; UniformSlot u; int ex = 1; s >> i >> u.name >> u.uniform >> u.comp >> ex; u.exported = ex != 0;
                if (vm.slots.size() <= i) vm.slots.resize(i + 1);
                vm.slots[i] = u;
            } else if (tag == "ucode") {
                size_t i, n; s >> i >> n;
                if (vm.code.size() <= i) vm.code.resize(i + 1);
                for (size_t k = 0; k < n; ++k) { int op; double a; s >> op >> a; vm.code[i].push_back({Op(op), a}); }
                if (!s) { error = "bad ucode at line " + std::to_string(lineNo); return false; }
            } else if (tag == "pass") {
                passes.emplace_back(); pass = &passes.back(); stage = nullptr;
                s >> pass->world >> pass->program >> pass->kind >> pass->source;
            } else if (!pass) continue;
            else if (tag == "targets") { int t; while (s >> t) pass->targets.push_back(t); }
            else if (tag == "blend") { std::string k, v; s >> k; std::getline(s >> std::ws, v); pass->blend[k] = v; }
            else if (tag == "alphatest") std::getline(s >> std::ws, pass->alphaTest);
            else if (tag == "stage") { std::string n; s >> n; stage = &pass->stages[n]; s >> stage->dxil >> stage->globalsSize; }
            else if (tag == "end") { pass = nullptr; stage = nullptr; }
            else if (!stage) continue;
            else if (tag == "g") { GlobalVar g; s >> g.name >> g.type >> g.offset; stage->globals.push_back(g); }
            else if (tag == "res") { Resource r; s >> r.name >> r.kind >> r.reg >> r.slot; stage->resources.push_back(r); }
            else if (tag == "in") { VsInput v; s >> v.sem >> v.index >> v.mask >> v.fmt; stage->inputs.push_back(v); }
            else if (tag == "def") { std::string k, v; s >> k >> v; stage->defaults[k] = v; }
        }
        vm.reset();
        return true;
    }
};

} // namespace rf::iris
