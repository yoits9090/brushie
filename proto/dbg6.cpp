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
static uint16_t adapt(uint16_t f1, uint32_t b, unsigned shift){
  int p = f1;
  if (b) { p -= p >> shift; if (p<1) p=1; } else { p += (4096-p)>>shift; if (p>4095) p=4095; }
  return (uint16_t)p;
}
int main(){
  auto bands = load("/tmp/syms.tsv");
  auto& band = bands[16];
  vector<uint16_t> probs(256, 2048);
  vector<uint16_t> fused(band.size());
  for (size_t i = 0; i < band.size(); ++i) {
    fused[i] = probs[band[i].ctx];
    probs[band[i].ctx] = adapt(probs[band[i].ctx], band[i].bit, band[i].pass==2?4:5);
  }
  uint32_t x = 1u << 16;
  uint64_t emits = 0;
  uint64_t ones = 0;
  for (size_t i = band.size(); i-- > 0;) {
    uint32_t b = band[i].bit;
    uint16_t f1 = fused[i];
    uint32_t f = (b ? f1 : (4096u-f1)) * 16u;
    uint32_t c = b ? (4096u-f1)*16u : 0u;
    if (x >= (f << 16)) {
      if (emits < 20) printf("emit at %zu: b=%u f1=%u x=%u\n", i, b, f1, x);
      x >>= 16; ++emits;
    }
    x = (x / f) * 65536u + (x % f) + c;
    ones += b;
  }
  printf("emits=%llu ones=%llu symbols=%zu\n", (unsigned long long)emits, (unsigned long long)ones, band.size());
  return 0;
}
