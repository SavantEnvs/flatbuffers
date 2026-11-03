// Mayhem copy of tests/fuzzer/flatbuffers_codegen_fuzzer.cc. Differences from upstream: IDLOptions::file_saver
// is set (flatc always sets it; left null the generators dereference it), to a saver that discards its
// output, and the generators get a private directory under $TMPDIR that is removed after each input,
// because several of them create directories named from the fuzzed namespaces even without writing files.
// It also rejects schemas with circular struct dependencies as flatc does, and prints the parsed JSON data
// (not the binary schema that Serialize() leaves in builder_, whose type is not root_struct_def_).
#include <bfbs_gen_lua.h>
#include <bfbs_gen_nim.h>
#include <flatbuffers/flatc.h>
#include <idl_gen_binary.h>
#include <idl_gen_csharp.h>
#include <idl_gen_dart.h>
#include <idl_gen_fbs.h>
#include <idl_gen_go.h>
#include <idl_gen_java.h>
#include <idl_gen_json_schema.h>
#include <idl_gen_kotlin.h>
#include <idl_gen_lobster.h>
#include <idl_gen_php.h>
#include <idl_gen_python.h>
#include <idl_gen_rust.h>
#include <idl_gen_swift.h>
#include <idl_gen_text.h>
#include <idl_gen_ts.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "flatbuffers/code_generator.h"
#include "flatbuffers/idl.h"  // For Parser and generation functions
#include "idl_gen_cpp.h"      // For C++ generator
#include "test_init.h"

static constexpr size_t kMinInputLength = 1;
static constexpr size_t kMaxInputLength = 16384;

static constexpr uint8_t flags_strict_json = 0x80;
static constexpr uint8_t flags_skip_unexpected_fields_in_json = 0x40;
static constexpr uint8_t flags_allow_non_utf8 = 0x20;

// Utility for test run.
OneTimeTestInit OneTimeTestInit::one_time_init_;

static const char* g_program_name = nullptr;

class DiscardFileSaver final : public flatbuffers::FileSaver {
 public:
  bool SaveFile(const char*, const char*, size_t, bool) override { return true; }
};

class ScratchDir {
 public:
  ScratchDir() {
    const char* tmp = getenv("TMPDIR");
    path_ = std::string(tmp && *tmp ? tmp : "/tmp") + "/flatc_fuzz_XXXXXX";
    TEST_NOTNULL(mkdtemp(&path_[0]));
    path_ += '/';
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

static void Warn(const flatbuffers::FlatCompiler* flatc,
                 const std::string& warn, bool show_exe_name) {
  (void)flatc;
  if (show_exe_name) {
    printf("%s: ", g_program_name);
  }
  fprintf(stderr, "\nwarning:\n  %s\n\n", warn.c_str());
}

static void Error(const flatbuffers::FlatCompiler* flatc,
                  const std::string& err, bool usage, bool show_exe_name) {
  if (show_exe_name) {
    printf("%s: ", g_program_name);
  }
  if (usage && flatc) {
    fprintf(stderr, "%s\n", flatc->GetShortUsageString(g_program_name).c_str());
  }
  fprintf(stderr, "\nerror:\n  %s\n\n", err.c_str());
  exit(1);
}

namespace flatbuffers {
void LogCompilerWarn(const std::string& warn) {
  Warn(static_cast<const flatbuffers::FlatCompiler*>(nullptr), warn, true);
}
void LogCompilerError(const std::string& err) {
  Error(static_cast<const flatbuffers::FlatCompiler*>(nullptr), err, false,
        true);
}
}  // namespace flatbuffers

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Reserve one byte for Parser flags and one byte for repetition counter.
  if (size < 3) return 0;
  const uint8_t flags = data[0];
  (void)data[1];  //  reserved
  data += 2;
  size -= 2;  // bypass

  const std::string original(reinterpret_cast<const char*>(data), size);
  auto input = std::string(original.c_str());  // until '\0'
  if (input.size() < kMinInputLength || input.size() > kMaxInputLength)
    return 0;

  static DiscardFileSaver file_saver;
  flatbuffers::IDLOptions opts;
  opts.file_saver = &file_saver;
  opts.strict_json = (flags & flags_strict_json);
  opts.skip_unexpected_fields_in_json =
      (flags & flags_skip_unexpected_fields_in_json);
  opts.allow_non_utf8 = (flags & flags_allow_non_utf8);

  flatbuffers::Parser parser(opts);

  // Guarantee 0-termination in the input.
  auto parse_input = input.c_str();

  // Parse the input schema
  if (parser.Parse(parse_input)) {
    // flatc rejects a schema with circular struct dependencies right after parsing and before it
    // generates anything (src/flatc.cpp, HasCircularStructDependency); the generators recurse
    // through struct fields without a depth limit and assume this check was made.
    if (parser.HasCircularStructDependency()) return -1;

    // builder_ holds the buffer of the JSON data, if the input contained any, and root_struct_def_
    // is its type: GenText needs exactly that pair (the text generator below uses it the same way).
    // Serialize() below replaces builder_'s contents with the binary schema, so this must come first.
    if (parser.root_struct_def_ && parser.builder_.GetSize()) {
      std::string json_output;
      flatbuffers::GenText(parser, parser.builder_.GetBufferPointer(),
                           &json_output);
    }

    std::string temp_filename = "fuzzer_generated";

    const std::string flatbuffers_version(flatbuffers::FLATBUFFERS_VERSION());
    std::vector<std::unique_ptr<flatbuffers::CodeGenerator>> generators;
    generators.emplace_back(flatbuffers::NewBinaryCodeGenerator());
    generators.emplace_back(flatbuffers::NewCppCodeGenerator());
    generators.emplace_back(flatbuffers::NewCSharpCodeGenerator());
    generators.emplace_back(flatbuffers::NewDartCodeGenerator());
    generators.emplace_back(flatbuffers::NewFBSCodeGenerator());
    generators.emplace_back(flatbuffers::NewGoCodeGenerator());
    generators.emplace_back(flatbuffers::NewJavaCodeGenerator());
    generators.emplace_back(flatbuffers::NewJsonSchemaCodeGenerator());
    generators.emplace_back(flatbuffers::NewKotlinCodeGenerator());
    generators.emplace_back(flatbuffers::NewKotlinKMPCodeGenerator());
    generators.emplace_back(flatbuffers::NewLobsterCodeGenerator());
    generators.emplace_back(
        flatbuffers::NewLuaBfbsGenerator(flatbuffers_version));
    generators.emplace_back(
        flatbuffers::NewNimBfbsGenerator(flatbuffers_version));
    generators.emplace_back(flatbuffers::NewPythonCodeGenerator());
    generators.emplace_back(flatbuffers::NewPhpCodeGenerator());
    generators.emplace_back(flatbuffers::NewRustCodeGenerator());
    generators.emplace_back(flatbuffers::NewTextCodeGenerator());
    generators.emplace_back(flatbuffers::NewSwiftCodeGenerator());
    generators.emplace_back(flatbuffers::NewTsCodeGenerator());

    ScratchDir scratch;
    for (auto& gen : generators) {
      auto p = gen.get();
      const std::string& temp_path = scratch.path();
      auto status = p->GenerateCode(parser, temp_path, "fuzzer_generated");
      if (status != flatbuffers::CodeGenerator::Status::OK) {
        TEST_OUTPUT_LINE("GenerateCode failed %d", status);
      }

      // test gRPC code generation
      auto grpc_status =
          p->GenerateGrpcCode(parser, temp_path, "fuzzer_generated");
      if (grpc_status != flatbuffers::CodeGenerator::Status::OK) {
        TEST_OUTPUT_LINE("GenerateGrpcCode failed %d", grpc_status);
      }
    }

    // Serialize the schema (this replaces builder_'s contents) and verify the result.
    parser.Serialize();
    const uint8_t* buf = parser.builder_.GetBufferPointer();
    flatbuffers::Verifier verifier(buf, parser.builder_.GetSize());
    TEST_EQ(true, reflection::VerifySchemaBuffer(verifier));
  }

  return 0;
}
