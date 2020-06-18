#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "global_variables.h"
#include "poke.h"
#include "util.h"

int nb_pokes = 100000;

uintptr_t poke(uintptr_t addr) {
  static uint64_t lastVirtualPage = -1;
  static uint64_t lastPhysPage = -1;

  register int i asm("eax");
  register uintptr_t ptr asm("ebx") = addr;
  uintptr_t paddr;

  for (i = 0; i < nb_pokes; i++) {
    clflush((void *)ptr);
  }

  if (addr >> 12 == lastVirtualPage) {
    paddr = lastPhysPage << 12 | (addr & 0xfffULL);
  } else {
    paddr = read_pagemap("/proc/self/pagemap", (uintptr_t)ptr);
    lastVirtualPage = addr >> 12;
    lastPhysPage = paddr >> 12;
  }
  return paddr;
}
