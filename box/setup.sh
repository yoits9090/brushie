#!/bin/bash
# Brushie CPU-box toolchain setup (Ubuntu 22.04 Colab VM). Idempotent. v2
set -x
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq build-essential clang llvm lldb valgrind strace time 2>&1 | tail -2
apt-get install -y -qq webp libwebp-dev libaom3 libaom-dev openjpeg-tools libopenjp2-tools 2>&1 | tail -2
apt-get install -y -qq libjxl-dev 2>&1 | tail -2 || echo "no libjxl-dev in jammy"
apt-get install -y -qq linux-tools-common 2>&1 | tail -1 || true
python3 -m pip install --quiet numpy pillow 2>&1 | tail -1 || true
echo "=== toolchain ==="
g++ --version | head -1
clang++ --version | head -1
valgrind --version
which cwebp dwebp opj_compress opj_decompress djxl cjxl 2>/dev/null || true
ffmpeg -version 2>/dev/null | head -1
echo "=== perf_event_open check ==="
cat > /tmp/pef.c <<'EOF'
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
static long peo(struct perf_event_attr* a, pid_t pid, int cpu, int g, unsigned long f){
  return syscall(__NR_perf_event_open, a, pid, cpu, g, f);
}
int main(){
  struct perf_event_attr a; memset(&a,0,sizeof(a));
  a.type=PERF_TYPE_HARDWARE; a.size=sizeof(a); a.config=PERF_COUNT_HW_INSTRUCTIONS;
  a.disabled=1; a.exclude_kernel=1; a.exclude_hv=1;
  long fd=peo(&a,0,-1,-1,0);
  if(fd<0){ printf("perf_event_open BLOCKED: %s\n", strerror(errno)); return 1;}
  printf("perf_event_open OK fd=%ld\n", fd); close(fd); return 0;
}
EOF
gcc -O2 /tmp/pef.c -o /tmp/pef && /tmp/pef || true
echo "SETUP2 DONE"
