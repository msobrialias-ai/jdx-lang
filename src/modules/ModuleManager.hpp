#pragma once
#include <string>
#include <unordered_map>
#include <optional>

namespace jdx::modules {

class ModuleManager {
public:
    ModuleManager(std::string projectRoot = ".");
    std::string resolveModule(const std::string& specifier, const std::string& currentFile) const;
    std::string readModuleSource(const std::string& resolvedPath) const;
    bool exists(const std::string& path) const;
private:
    std::string projectRoot_;
};

} // namespace jdx::modules
