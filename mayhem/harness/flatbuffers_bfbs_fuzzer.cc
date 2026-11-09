// Feeds the fuzzed bytes to flatbuffers::Parser::Deserialize, the library's reader for binary schemas
// (.bfbs); `flatc` calls it on every .bfbs schema it is given (src/flatc.cpp, LoadBinarySchema).
#include <stddef.h>
#include <stdint.h>

#include "flatbuffers/idl.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Parser::Deserialize(buf, size) tests the file identifier at byte offset 4, or 8 for a size-prefixed
  // buffer, before it looks at `size` at all, so a buffer shorter than 12 bytes is not a schema buffer.
  if (size < 12) return -1;
  flatbuffers::Parser parser;
  parser.Deserialize(data, size);
  return 0;
}
