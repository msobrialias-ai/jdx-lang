#include "interpreter/Environment.hpp"

namespace jdx::interpreter {

Environment::Environment(std::shared_ptr<Environment> parent) : parent_(std::move(parent)) {}

void Environment::define(const std::string& name, const runtime::Value& value, bool isConst, bool isExported) {
    values_[name] = Binding{value, isConst, isExported};
    if (isExported) {
        exports_[name] = name;
    }
}

bool Environment::assign(const std::string& name, const runtime::Value& value) {
    auto it = values_.find(name);
    if (it != values_.end()) {
        if (it->second.isConst) return false;
        it->second.value = value;
        return true;
    }
    return parent_ ? parent_->assign(name, value) : false;
}

bool Environment::get(const std::string& name, runtime::Value& out) const {
    auto it = values_.find(name);
    if (it != values_.end()) {
        out = it->second.value;
        return true;
    }
    return parent_ ? parent_->get(name, out) : false;
}

bool Environment::getLocal(const std::string& name, runtime::Value& out) const {
    auto it = values_.find(name);
    if (it == values_.end()) return false;
    out = it->second.value;
    return true;
}

bool Environment::existsLocal(const std::string& name) const { return values_.find(name) != values_.end(); }

std::vector<std::string> Environment::localNames() const {
    std::vector<std::string> out;
    out.reserve(values_.size());
    for (const auto& [k, _] : values_) out.push_back(k);
    return out;
}

void Environment::exportName(const std::string& localName, const std::string& exportedName) {
    exports_[exportedName] = localName;
}

void Environment::setDefaultExport(const runtime::Value& value) {
    defaultExport_ = value;
}

bool Environment::hasDefaultExport() const {
    return defaultExport_.has_value();
}

} // namespace jdx::interpreter
