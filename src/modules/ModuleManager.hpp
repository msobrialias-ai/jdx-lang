
#pragma once

#include <string>

namespace jdx::modules {

class ModuleManager final {
public:
    explicit ModuleManager(std::string projectRoot = ".");

    [[nodiscard]] std::string resolveModule(const std::string& specifier, const std::string& currentFile) const;
    [[nodiscard]] std::string readModuleSource(const std::string& resolvedPath) const;
    [[nodiscard]] bool exists(const std::string& path) const;

private:
    std::string projectRoot_;
};

} // namespace jdx::modules
