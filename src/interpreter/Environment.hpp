#pragma once
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "runtime/Value.hpp"

namespace jdx::interpreter {

class Environment : public std::enable_shared_from_this<Environment> {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr);

    void define(const std::string& name,
                const runtime::Value& value,
                bool isConst = false,
                bool isExported = false);
    bool assign(const std::string& name, const runtime::Value& value);
    bool get(const std::string& name, runtime::Value& out) const;
    bool getLocal(const std::string& name, runtime::Value& out) const;

    bool existsLocal(const std::string& name) const;
    std::vector<std::string> localNames() const;

    void exportName(const std::string& localName, const std::string& exportedName);
    void setDefaultExport(const runtime::Value& value);
    bool hasDefaultExport() const;
    const std::optional<runtime::Value>& defaultExport() const { return defaultExport_; }
    const std::unordered_map<std::string, std::string>& exports() const { return exports_; }

    std::shared_ptr<Environment> parent() const { return parent_; }
private:
    struct Binding {
        runtime::Value value;
        bool isConst {false};
        bool isExported {false};
    };

    std::unordered_map<std::string, Binding> values_;
    std::unordered_map<std::string, std::string> exports_;
    std::optional<runtime::Value> defaultExport_;
    std::shared_ptr<Environment> parent_;
};

} // namespace jdx::interpreter
