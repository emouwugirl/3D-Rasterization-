// obj2bin: converts an OBJ or PLY model into a .bin mesh for the renderers.
#include <cstdio>
#include <string>
#include <vector>

#include "cli.hpp"

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  return rasty::convert::run_obj2bin(args, stdout, stderr);
}
