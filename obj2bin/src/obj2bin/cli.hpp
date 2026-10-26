// The obj2bin command line, as a function that tests can call (internal header).
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace rasty::convert {

// Runs obj2bin with the given arguments (without the program name), printing the summary to `out` and
// warnings and errors to `err`. Returns the exit status: 0 on success, 1 if the conversion failed, 2 for
// bad usage.
int run_obj2bin(const std::vector<std::string>& args, std::FILE* out, std::FILE* err);

}  // namespace rasty::convert
