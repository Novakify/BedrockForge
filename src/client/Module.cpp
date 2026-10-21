#include "client/Module.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace bedrock_forge::client {

char const* categoryName(Category c) {
    switch (c) {
    case Category::Hud: return "HUD";
    case Category::Combat: return "Combat";
    case Category::Camera: return "Camera";
    case Category::Visual: return "Visual";
    case Category::Movement: return "Movement";
    case Category::World: return "World";
    default: return "Misc";
    }
}

Setting* Module::setting(std::string const& sid) {
    for (auto& s : settings)
        if (s.id == sid) return &s;
    return nullptr;
}
Module& Module::toggleSetting(std::string sid, std::string label, bool on) {
    Setting s; s.kind = Setting::Kind::Toggle; s.id = std::move(sid); s.label = std::move(label); s.on = on;
    settings.push_back(std::move(s));
    return *this;
}
Module& Module::slider(std::string sid, std::string label, float value, float min, float max, float step) {
    Setting s; s.kind = Setting::Kind::Slider; s.id = std::move(sid); s.label = std::move(label);
    s.value = value; s.min = min; s.max = max; s.step = step;
    settings.push_back(std::move(s));
    return *this;
}

Registry& Registry::get() {
    static Registry r;
    return r;
}
Module& Registry::add(std::string id, std::string name, Category cat, std::string description, bool enabledByDefault) {
    auto m = std::make_unique<Module>();
    m->id = std::move(id); m->name = std::move(name); m->category = cat; m->description = std::move(description);
    m->enabled = enabledByDefault;
    mModules.push_back(std::move(m));
    return *mModules.back();
}
Module& Registry::plan(std::string id, std::string name, Category cat, std::string description) {
    auto& m   = add(std::move(id), std::move(name), cat, std::move(description));
    m.planned = true;
    return m;
}
Module* Registry::find(std::string const& id) {
    for (auto& m : mModules)
        if (m->id == id) return m.get();
    return nullptr;
}
void Registry::setEnabled(Module& m, bool on) {
    if (m.planned || m.enabled == on) return;
    m.enabled = on;
    if (m.onToggle) m.onToggle(on);
    save();
}

void Registry::load(std::string const& path) {
    mPath = path;
    std::ifstream in(std::filesystem::path(path));
    if (!in) { save(); return; }
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    for (auto& m : mModules) {
        auto it = j.find(m->id);
        if (it == j.end() || !it->is_object()) continue;
        if (auto e = it->find("enabled"); e != it->end() && e->is_boolean() && !m->planned) m->enabled = e->get<bool>();
        if (auto k = it->find("key"); k != it->end() && k->is_number_integer()) m->key = k->get<int>();
        if (auto s = it->find("settings"); s != it->end() && s->is_object())
            for (auto& st : m->settings) {
                auto v = s->find(st.id);
                if (v == s->end()) continue;
                if (st.kind == Setting::Kind::Toggle && v->is_boolean()) st.on = v->get<bool>();
                if (st.kind == Setting::Kind::Slider && v->is_number()) st.value = v->get<float>();
            }
    }
}

void Registry::save() const {
    if (mPath.empty()) return;
    nlohmann::json j = nlohmann::json::object();
    for (auto& m : mModules) {
        nlohmann::json s = nlohmann::json::object();
        for (auto& st : m->settings) {
            if (st.kind == Setting::Kind::Toggle) s[st.id] = st.on;
            else s[st.id] = st.value;
        }
        j[m->id] = {{"enabled", m->enabled}, {"key", m->key}, {"settings", s}};
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(mPath).parent_path(), ec);
    std::ofstream out(std::filesystem::path(mPath));
    out << j.dump(2);
}

} // namespace bedrock_forge::client
