#include "modules/ModuleManager.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace jdx::modules {

ModuleManager::ModuleManager(std::string projectRoot) : projectRoot_(std::move(projectRoot)) {}

bool ModuleManager::exists(const std::string& path) const { return fs::exists(fs::path(path)); }

std::string ModuleManager::resolveModule(const std::string& specifier, const std::string& currentFile) const {
    fs::path spec(specifier);
    if (specifier.rfind("jdx:", 0) == 0) {
        std::string name = specifier.substr(4);
        fs::path p = fs::path(projectRoot_) / "src" / "modules" / (name + ".jdx");
        if (exists(p.string())) return fs::weakly_canonical(p).string();
        p = fs::path(projectRoot_) / "src" / "modules" / name / "index.jdx";
        if (exists(p.string())) return fs::weakly_canonical(p).string();
        throw std::runtime_error("Runtime Error: Unable to resolve internal module '" + specifier + "'.");
    }
    if (spec.is_relative()) {
        fs::path base = fs::path(currentFile).has_parent_path() ? fs::path(currentFile).parent_path() : fs::current_path();
        fs::path p = base / spec;
        if (exists(p.string())) return fs::weakly_canonical(p).string();
        p = fs::path(projectRoot_) / spec;
        if (exists(p.string())) return fs::weakly_canonical(p).string();
    }
    if (exists(spec.string())) return fs::weakly_canonical(spec).string();

    fs::path external = fs::path(projectRoot_) / "jdx_modules" / specifier / "index.jdx";
    if (exists(external.string())) return fs::weakly_canonical(external).string();
    external = fs::path(projectRoot_) / "jdx_modules" / (specifier + ".jdx");
    if (exists(external.string())) return fs::weakly_canonical(external).string();
    throw std::runtime_error("Runtime Error: Unable to resolve module '" + specifier + "'.");
}

std::string ModuleManager::readModuleSource(const std::string& resolvedPath) const {
    std::ifstream in(resolvedPath, std::ios::binary);
    if (!in) throw std::runtime_error("Runtime Error: Unable to open module file '" + resolvedPath + "'.");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace jdx::modules
