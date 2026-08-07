// Fuzz driver: decode a stream a few ways; used by tests/fuzz.py.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "brushie/codec.h"

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  const long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<std::uint8_t> data(static_cast<std::size_t>(sz));
  if (fread(data.data(), 1, data.size(), f) != data.size()) return 2;
  fclose(f);
  std::vector<std::uint8_t> rgb;
  std::string error;
  const std::uint32_t w = sz >= 12 ? (static_cast<std::uint32_t>(data[8]) |
                                      (static_cast<std::uint32_t>(data[9]) << 8) |
                                      (static_cast<std::uint32_t>(data[10]) << 16) |
                                      (static_cast<std::uint32_t>(data[11]) << 24)) : 16;
  const std::uint32_t h = sz >= 16 ? (static_cast<std::uint32_t>(data[12]) |
                                      (static_cast<std::uint32_t>(data[13]) << 8) |
                                      (static_cast<std::uint32_t>(data[14]) << 16) |
                                      (static_cast<std::uint32_t>(data[15]) << 24)) : 16;
  for (const int layer : {-1, 0, 1, 5}) {
    (void)brushie::decode(data.data(), data.size(), w > 0 ? w : 16, h > 0 ? h : 16,
                          rgb, layer, &error);
  }
  return 0;
}
