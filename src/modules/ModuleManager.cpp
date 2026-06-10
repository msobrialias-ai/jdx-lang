#include "modules/ModuleManager.hpp"
#include "generated/EmbeddedModules.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace jdx::modules {

namespace {
constexpr const char* kEmbeddedPrefix = "__embedded__/";
}

ModuleManager::ModuleManager(std::string projectRoot)
    : projectRoot_(std::move(projectRoot)) {}

bool ModuleManager::exists(const std::string& path) const {
    return fs::exists(fs::path(path));
}

std::string ModuleManager::resolveModule(const std::string& specifier, const std::string& currentFile) const {
    const fs::path spec(specifier);

    if (specifier == "Develoment.Test") {
        const std::string key = "Develoment.Test";
        if (hasEmbeddedModule(key)) {
            return std::string(kEmbeddedPrefix) + key;
        }

        const fs::path local = fs::path(projectRoot_) / "src" / "modules" / "Develoment.Test.jdx";
        if (exists(local.string())) {
            return fs::weakly_canonical(local).string();
        }
    }

    if (specifier.rfind("jdx:", 0U) == 0U) {
        const std::string name = specifier.substr(4U);

        if (hasEmbeddedModule(name)) {
            return std::string(kEmbeddedPrefix) + name;
        }

        const fs::path localFlat = fs::path(projectRoot_) / "src" / "modules" / (name + ".jdx");
        if (exists(localFlat.string())) {
            return fs::weakly_canonical(localFlat).string();
        }

        const fs::path localIndex = fs::path(projectRoot_) / "src" / "modules" / name / "index.jdx";
        if (exists(localIndex.string())) {
            return fs::weakly_canonical(localIndex).string();
        }

        throw std::runtime_error("Runtime Error: Unable to resolve internal module '" + specifier + "'.");
    }

    if (spec.is_relative()) {
        const fs::path base = fs::path(currentFile).has_parent_path()
            ? fs::path(currentFile).parent_path()
            : fs::current_path();

        fs::path candidate = base / spec;
        if (exists(candidate.string())) {
            return fs::weakly_canonical(candidate).string();
        }

        candidate = fs::path(projectRoot_) / spec;
        if (exists(candidate.string())) {
            return fs::weakly_canonical(candidate).string();
        }
    }

    if (exists(spec.string())) {
        return fs::weakly_canonical(spec).string();
    }

    fs::path external = fs::path(projectRoot_) / "jdx_modules" / specifier / "index.jdx";
    if (exists(external.string())) {
        return fs::weakly_canonical(external).string();
    }

    external = fs::path(projectRoot_) / "jdx_modules" / (specifier + ".jdx");
    if (exists(external.string())) {
        return fs::weakly_canonical(external).string();
    }

    throw std::runtime_error("Runtime Error: Unable to resolve module '" + specifier + "'.");
}

std::string ModuleManager::readModuleSource(const std::string& resolvedPath) const {
    if (resolvedPath.rfind(kEmbeddedPrefix, 0U) == 0U) {
        const std::string key = resolvedPath.substr(std::char_traits<char>::length(kEmbeddedPrefix));
        auto it = EmbeddedModules.find(key);

        if (it == EmbeddedModules.end()) {
            throw std::runtime_error("Runtime Error: Embedded module not found '" + key + "'.");
        }

        return std::string(it->second);
    }

    std::ifstream in(resolvedPath, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Runtime Error: Unable to open module file '" + resolvedPath + "'.");
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace jdx::modules