#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unordered_map>
using namespace std;
struct Sym { uint32_t pass; uint32_t ctx; uint32_t bit; };
static vector<vector<Sym>> load(const char* path) {
  FILE* f = fopen(path, "r");
  vector<vector<Sym>> bands;
  unordered_map<string, int> tag2band;
  char tag[64], pass[16];
  int ctx, bit;
  while (fscanf(f, "%63s %15s %d %d", tag, pass, &ctx, &bit) == 4) {
    uint32_t pid = 0;
    if (!strcmp(pass, "sign")) pid = 1;
    else if (!strcmp(pass, "unary")) pid = 2;
    else if (!strcmp(pass, "rem")) pid = 3;
    else if (!strcmp(pass, "blk")) pid = 4;
    int b;
    auto it = tag2band.find(tag);
    if (it == tag2band.end()) { b = (int)bands.size(); tag2band[tag] = b; bands.emplace_back(); }
    else b = it->second;
    bands[b].push_back(Sym{pid, (uint32_t)ctx, (uint32_t)bit});
  }
  fclose(f);
  return bands;
}
struct RangeEnc {
  std::vector<uint8_t>& out;
  uint64_t low = 0; uint32_t range = 0xFFFFFFFFu;
  uint8_t cache = 0; size_t cache_size = 1;
  RangeEnc(std::vector<uint8_t>& o) : out(o) {}
  void shift_low() {
    if (low < 0xFF000000ull || low > 0xFFFFFFFFull) {
      uint8_t carry = (uint8_t)(low >> 32);
      out.push_back((uint8_t)(cache + carry));
      for (size_t i = 1; i < cache_size; ++i) out.push_back((uint8_t)(0xFF + carry));
      cache = (uint8_t)(low >> 24); cache_size = 1;
    } else ++cache_size;
    low = (low << 8) & 0xFFFFFFFFull;
  }
  void bit(uint16_t& prob, uint32_t b, unsigned shift) {
    uint32_t bound = (uint32_t)(((uint64_t)range >> 11) * prob);
    if (b == 0) { range = bound; int p = prob; p += (2048 - p) >> shift; prob = (uint16_t)(p < 2048 ? p : 2047); }
    else { low += bound; range -= bound; int p = prob; p -= p >> shift; prob = (uint16_t)(p > 0 ? p : 1); }
    while (range < (1u << 24)) { range <<= 8; shift_low(); }
  }
  void flush() { for (int i = 0; i < 5; ++i) shift_low(); }
};
struct Enc {
  std::vector<uint8_t>& out; uint32_t x = 1u << 16; std::vector<uint16_t> chunks;
  Enc(std::vector<uint8_t>& o):out(o){}
  void bit(uint16_t f1, uint32_t b){
    uint32_t f = (b ? f1 : (4096u-f1)) * 16u;
    uint32_t c = b ? (4096u-f1)*16u : 0u;
    while (x >= (f << 16)) { chunks.push_back((uint16_t)(x & 0xFFFF)); x >>= 16; }
    x = (x / f) * 65536u + (x % f) + c;
  }
  void flush(){
    out.push_back((uint8_t)(x>>24)); out.push_back((uint8_t)(x>>16));
    out.push_back((uint8_t)(x>>8)); out.push_back((uint8_t)x);
    for (size_t i = chunks.size(); i-- > 0;) {
      out.push_back((uint8_t)(chunks[i] & 0xFF)); out.push_back((uint8_t)(chunks[i] >> 8));
    }
  }
};
static uint16_t adapt(uint16_t f1, uint32_t b, unsigned shift){
  int p = f1;
  if (b) { p -= p >> shift; if (p<1) p=1; } else { p += (4096-p)>>shift; if (p>4095) p=4095; }
  return (uint16_t)p;
}
int main(){
  auto bands = load("/tmp/syms.tsv");
  for (size_t bi : {0u, 11u, 16u}) {
    auto& band = bands[bi];
    vector<uint16_t> probs(256, 2048);
    vector<uint16_t> fused(band.size());
    for (size_t i = 0; i < band.size(); ++i) {
      fused[i] = probs[band[i].ctx];
      probs[band[i].ctx] = adapt(probs[band[i].ctx], band[i].bit, band[i].pass==2?4:5);
    }
    vector<uint8_t> rng;
    {
      RangeEnc e(rng);
      vector<uint16_t> p2(256, 2047);
      for (auto& s : band) e.bit(p2[s.ctx], s.bit, s.pass==2?4:5);
      e.flush();
    }
    vector<uint8_t> rans;
    {
      Enc e(rans);
      for (size_t i = band.size(); i-- > 0;) e.bit(fused[i], band[i].bit);
      e.flush();
    }
    printf("band %zu: symbols=%zu range=%zu rans=%zu\n", bi, band.size(), rng.size(), rans.size());
  }
  return 0;
}
