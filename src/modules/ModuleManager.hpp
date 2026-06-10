#pragma once

#include <string>

namespace jdx::modules {

class ModuleManager {
public:
    explicit ModuleManager(std::string projectRoot);

    bool exists(const std::string& path) const;
    std::string resolveModule(const std::string& specifier, const std::string& currentFile) const;
    std::string readModuleSource(const std::string& resolvedPath) const;

private:
    std::string projectRoot_;
};

} // namespace jdx::modules