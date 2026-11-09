// Verifier over the Monster test schema with alignment checking turned off, the documented
// option (Verifier's `_check_alignment` argument) for buffers that are not naturally aligned.
// VerifyTableStart is the code under test: with alignment checks on, an odd vtable size is rejected
// as a side effect of VerifyAlignment, so only this configuration reaches the vtable-size check.
#include <stddef.h>
#include <stdint.h>

#include "cpp17/generated_cpp17/monster_test_generated.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  flatbuffers::Verifier verifier(data, size, 64, 1000000, false);
  MyGame::Example::VerifyMonsterBuffer(verifier);
  return 0;
}
