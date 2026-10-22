// The obj2bin command line: options, output naming, what it prints, and exit codes.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cli.hpp"
#include "test_util.hpp"

namespace rasty::convert {
namespace {

using test::read_bytes;
using test::TempDir;
using test::write_file;

struct CliResult {
  int status;
  std::string out, err;
};

std::string drain(std::FILE* f) {
  std::rewind(f);
  std::string s;
  char buf[4096];
  for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) s.append(buf, n);
  std::fclose(f);
  return s;
}

CliResult obj2bin(const std::vector<std::string>& args) {
  std::FILE* out = std::tmpfile();
  std::FILE* err = std::tmpfile();
  const int status = run_obj2bin(args, out, err);
  return CliResult{status, drain(out), drain(err)};
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

const char* const kQuad = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n";

TEST(Cli, WritesNextToTheInputByDefault) {
  TempDir dir;
  write_file(dir.file("quad.obj"), kQuad);
  const CliResult r = obj2bin({dir.file("quad.obj")});
  EXPECT_EQ(r.status, 0) << r.err;
  EXPECT_TRUE(std::filesystem::exists(dir.file("quad.bin")));
  EXPECT_TRUE(contains(r.out, "triangles 2\n"));
  EXPECT_TRUE(contains(r.out, "computed (angle-weighted)"));
  EXPECT_TRUE(contains(r.out, "wrote     " + dir.file("quad.bin")));
  EXPECT_EQ(r.err, "");
}

TEST(Cli, OptionsReachTheConverter) {
  TempDir dir;
  write_file(dir.file("c.obj"), "v 0 0 0 1 0 0\nv 1 0 0 0 1 0\nv 0 1 0 0 0 1\nvn 0 0 1\nf 1//1 2//1 3//1\n");
  const CliResult plain = obj2bin({dir.file("c.obj"), "-o", dir.file("a.bin")});
  EXPECT_TRUE(contains(plain.out, "colors    yes"));
  EXPECT_TRUE(contains(plain.out, "3 from the file"));
  const CliResult options = obj2bin({"--no-colors", "--recompute-normals", dir.file("c.obj"), "--output", dir.file("b.bin")});
  EXPECT_EQ(options.status, 0) << options.err;
  EXPECT_TRUE(contains(options.out, "colors    none"));
  EXPECT_TRUE(contains(options.out, "0 from the file, 3 computed"));
}

TEST(Cli, QuietPrintsOnlyWarningsAndErrors) {
  TempDir dir;
  write_file(dir.file("q.obj"), std::string("bevel on\n") + kQuad);
  const CliResult r = obj2bin({"-q", dir.file("q.obj"), "-o", dir.file("q.mesh")});
  EXPECT_EQ(r.status, 0) << r.err;
  EXPECT_EQ(r.out, "");
  EXPECT_TRUE(contains(r.err, "warning: the output name '" + dir.file("q.mesh") + "' does not end in .bin"));
  EXPECT_TRUE(contains(r.err, "ignoring unsupported statement 'bevel'"));
}

TEST(Cli, RefusesToOverwriteTheInput) {
  TempDir dir;
  write_file(dir.file("m.obj"), kQuad);
  const CliResult explicit_output = obj2bin({dir.file("m.obj"), "-o", dir.file("m.obj")});
  EXPECT_EQ(explicit_output.status, 1);
  EXPECT_TRUE(contains(explicit_output.err, "is the input file"));
  EXPECT_EQ(read_bytes(dir.file("m.obj")), kQuad);

  // A PLY file named .bin: the default output name is the input itself.
  const std::string ply =
      "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
      "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
  write_file(dir.file("scan.bin"), ply);
  EXPECT_EQ(obj2bin({dir.file("scan.bin")}).status, 1);
  EXPECT_EQ(read_bytes(dir.file("scan.bin")), ply);
}

TEST(Cli, WarningsArePrintedWhenTheConversionFails) {
  TempDir dir;
  // Every triangle is degenerate, so building the mesh fails after the reader has warned.
  write_file(dir.file("flat.obj"), "bevel on\nv 0 0 0\nv 0 0 0\nv 1 0 0\nf 1 2 3\n");
  const CliResult build_fails = obj2bin({dir.file("flat.obj")});
  EXPECT_EQ(build_fails.status, 1);
  EXPECT_TRUE(contains(build_fails.err, "warning: " + dir.file("flat.obj") + ":1: ignoring unsupported statement 'bevel'"));
  EXPECT_TRUE(contains(build_fails.err, "error: no triangles left"));

  // Writing fails: the folder does not exist.
  write_file(dir.file("w.obj"), std::string("bevel on\n") + kQuad);
  const CliResult write_fails = obj2bin({dir.file("w.obj"), "-o", dir.file("missing/w.bin")});
  EXPECT_EQ(write_fails.status, 1);
  EXPECT_TRUE(contains(write_fails.err, "ignoring unsupported statement 'bevel'"));
  EXPECT_TRUE(contains(write_fails.err, "error: cannot open"));
}

TEST(Cli, UsageErrors) {
  const CliResult none = obj2bin({});
  EXPECT_EQ(none.status, 2);
  EXPECT_TRUE(contains(none.err, "usage: obj2bin"));
  const CliResult two = obj2bin({"a.obj", "b.obj"});
  EXPECT_EQ(two.status, 2);
  EXPECT_TRUE(contains(two.err, "give one input file (got 2)"));
  const CliResult unknown = obj2bin({"--fast", "a.obj"});
  EXPECT_EQ(unknown.status, 2);
  EXPECT_TRUE(contains(unknown.err, "unknown option '--fast'"));
  const CliResult no_path = obj2bin({"a.obj", "-o"});
  EXPECT_EQ(no_path.status, 2);
  EXPECT_TRUE(contains(no_path.err, "-o needs a path"));
  const CliResult help = obj2bin({"--help"});
  EXPECT_EQ(help.status, 0);
  EXPECT_TRUE(contains(help.out, "usage: obj2bin"));
  const CliResult missing = obj2bin({"no-such-file.obj"});
  EXPECT_EQ(missing.status, 1);
  EXPECT_TRUE(contains(missing.err, "error: cannot open 'no-such-file.obj'"));
}

}  // namespace
}  // namespace rasty::convert
