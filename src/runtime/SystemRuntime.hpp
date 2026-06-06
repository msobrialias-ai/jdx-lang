#pragma once
#include <string>
#include <vector>
#include "runtime/Value.hpp"

namespace jdx::interpreter { class Interpreter; }

namespace jdx::runtime {

Value makeSystemObject(const std::vector<std::string>& args);
Value makeNative(const std::string& name, std::function<Value(const std::vector<Value>&)> fn);

} // namespace jdx::runtime
