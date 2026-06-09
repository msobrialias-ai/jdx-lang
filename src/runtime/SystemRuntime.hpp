
#pragma once

#include <string>
#include <vector>

#include "runtime/Value.hpp"

namespace jdx::interpreter {
class Interpreter;
class Environment;
}

namespace jdx::runtime {

Value makeSystemObject(const std::vector<std::string>& args);
Value makeDevelopmentObject();

std::shared_ptr<interpreter::Environment> makeGlobalEnvironment(const std::vector<std::string>& args);

} // namespace jdx::runtime
