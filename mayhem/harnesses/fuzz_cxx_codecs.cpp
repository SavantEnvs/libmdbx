// mdbx C++ API (mdbx.c++): hex / base58 / base64 decoders and encoders on an
// arbitrary slice. Decode results are re-encoded and decoded again; the
// round-trip must give back the same bytes.
#include "mdbx.h++"
#include <cstdint>
#include <cstdlib>
#include <exception>

static void roundtrip_check(const mdbx::slice &decoded, int which) {
  mdbx::slice d(decoded);
  mdbx::buffer<> again;
  switch (which) {
  case 0:
    again = d.encode_hex().hex_decode();
    break;
  case 1:
    again = d.encode_base58().base58_decode();
    break;
  default:
    again = d.encode_base64().base64_decode();
    break;
  }
  if (again.slice() != d)
    abort();  // codec round-trip mismatch is a real bug
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return -1;
  const bool spaces = data[0] & 1;
  const mdbx::slice in(data + 1, size - 1);
  (void)in.is_printable(data[0] & 2);
  (void)in.is_hex(spaces);
  (void)in.is_base58(spaces);
  (void)in.is_base64(spaces);
  try {
    roundtrip_check(in.hex_decode(spaces).slice(), 0);
  } catch (const std::exception &) {
  }
  try {
    roundtrip_check(in.base58_decode(spaces).slice(), 1);
  } catch (const std::exception &) {
  }
  try {
    roundtrip_check(in.base64_decode(spaces).slice(), 2);
  } catch (const std::exception &) {
  }
  try {
    roundtrip_check(in, data[0] % 3);
    (void)in.encode_hex(data[0] & 4, data[0] >> 3);
    (void)in.encode_base64(data[0] >> 2);
  } catch (const std::exception &) {
  }
  return 0;
}
