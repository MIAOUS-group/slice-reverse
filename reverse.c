#define _GNU_SOURCE
#include <getopt.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cpuid.h"
#include "global_variables.h"
#include "monitoring.h"
#include "poke.h"
#include "rdmsr.h"
#include "reverse.h"
#include "util.h"
#include "wrmsr.h"

#define HUGE_PAGE_SIZE (1 * 1024 * 1024 * 1024)
#define HUGE_PAGE_SIZE_2M (2 * 1024 * 1024)
#define ADDR_PER_BIT 500
#define THRESHOLD 200
#define DEBUG 1

void print_help() {
  fprintf(stderr, "\nUsage: sudo ./reverse\n\
Options:\n\
--help -h      prints this help\n\
--clflush -f   does not use performance counters but clflush method\n\
--scan -s      does not reverse-engineer the function but finds the slice for a few addresses\n");
}

/*
 * Declare global variables
 */

char *archi;
char *class; // xeon or core
int nb_cores;
int max_slices;
int clflush = 0;
int scan = 0;

// Xeons MSRs and values
unsigned long long *msr_pmon_ctr0 = NULL;
unsigned long long *msr_pmon_box_filter = NULL;
unsigned long long *msr_pmon_ctl0 = NULL;
unsigned long long *msr_pmon_box_ctl = NULL;
unsigned long long val_box_freeze = -1;
unsigned long long val_box_reset = -1;
unsigned long long val_enable_counting = -1;
unsigned long long val_select_event = -1;
unsigned long long val_filter = -1;
unsigned long long val_box_unfreeze = -1;

// Core MSRs and values
unsigned long long msr_unc_perf_global_ctr = -1;
unsigned long long *msr_unc_cbo_perfevtsel0 = NULL;
unsigned long long *msr_unc_cbo_per_ctr0 = NULL;
unsigned long long val_enable_ctrs = -1;
unsigned long long val_disable_ctrs = -1;
unsigned long long val_select_evt_core = -1;
unsigned long long val_reset_ctrs = -1;

int main(int argc, char **argv) {

  /*
   * Verify CPU is Intel
   */
  if (!is_intel()) {
    printf("CPU is not Intel\n");
    exit(EXIT_FAILURE);
  }

  /*
   * Pin to core
   */
  int cpu_mask = 0;
  cpu_set_t my_set;
  CPU_ZERO(&my_set);
  CPU_SET(cpu_mask, &my_set);
  sched_setaffinity(0, sizeof(cpu_set_t), &my_set);

  /*
   * Extract CPU information: micro-arch name and number of cores
   * https://en.wikichip.org/wiki/intel/cpuid
   */
  nb_cores = cores_per_package();
  int cpu_model = get_cpu_model();

  // CPU class: Xeon or Core
  switch (cpu_model) {
  case 45:
  case 62:
  case 63:
  case 86:
  case 79:
  case 85:
    class = "xeon";
    break;
  case 42:
  case 58:
  case 60:
  case 69:
  case 70:
  case 61:
  case 71:
  case 78:
  case 94:
  case 142:
  case 158:
    class = "core";
    break;
  default:
    class = "undefined";
    printf("CPU is undefined\n");
    exit(EXIT_FAILURE);
  }

  // CPU micro-architecture
  switch (cpu_model) {
  case 45:
  case 42:
    archi = "sdb"; // Sandy Bridge
    break;
  case 62:
  case 58:
    archi = "ivb"; // Ivy Bridge
    break;
  case 63:
  case 60:
  case 69:
  case 70:
    archi = "hsw"; // Haswell
    break;
  case 86:
  case 79:
  case 61:
  case 71:
    archi = "bdw"; // Broadwell
    break;
  case 78:
  case 94:
    archi = "skl"; // Skylake (core)
    break;
  case 85:
    archi = "skl"; // Skyake (xeon) -> not supported yet
    printf("Micro-architecure not supported (Xeon Skylake)\n");
    exit(EXIT_FAILURE);
  case 142:
  case 158:
    archi = "kbl"; // Kaby Lake or Coffee Lake
    break;
  default:
    archi = "undefined";
    printf("Micro-architecture is undefined\n");
    exit(EXIT_FAILURE);
  }

  printf("Micro-architecture: %s %s\n", class, archi);
  printf("Number of cores: %d\n", nb_cores);

  /*
   * Options
   */
  int opt;

  static struct option long_options[] = {{"help", no_argument, NULL, 'h'},
                                         {"clflush", no_argument, NULL, 'f'},
                                         {"scan", no_argument, NULL, 's'},
                                         {NULL, 0, NULL, 0}};

  while ((opt = getopt_long(argc, argv, "hfs", long_options, NULL)) != -1) {
    // check to see if a single character or long option came through
    switch (opt) {
    case 'h':
      print_help();
      exit(0);
    case 'f':
      clflush = 1;
      break;
    case 's':
      scan = 1;
      break;
    default:
      print_help();
      exit(0);
    }
  }

  /*
   * Initialize architecture-dependent variables
   */

  // Xeons
  if (!strcmp(class, "xeon")) {
    if (!strcmp(archi, "sdb")) {
      max_slices = 8;
      msr_pmon_ctr0 = (unsigned long long[]){0xd16, 0xd36, 0xd56, 0xd76,
                                             0xd96, 0xdb6, 0xdd6, 0xdf6};
      msr_pmon_box_filter = (unsigned long long[]){0xd14, 0xd34, 0xd54, 0xd74,
                                                   0xd94, 0xdb4, 0xdd4, 0xdf4};
      msr_pmon_ctl0 = (unsigned long long[]){0xd10, 0xd30, 0xd50, 0xd70,
                                             0xd90, 0xdb0, 0xdd0, 0xdf0};
      msr_pmon_box_ctl = (unsigned long long[]){0xd04, 0xd24, 0xd44, 0xd64,
                                                0xd84, 0xda4, 0xdc4, 0xde4};
      val_box_freeze = 0x10100;
      val_box_reset = 0x10103;
      val_enable_counting = 0x400000;
      val_select_event = 0x401134;
      val_filter = 0x7c0000;
      val_box_unfreeze = 0x10000;
    } else if (!strcmp(archi, "ivb")) {
      max_slices = 15;
      msr_pmon_ctr0 = (unsigned long long[]){0xd16, 0xd36, 0xd56, 0xd76, 0xd96,
                                             0xdb6, 0xdd6, 0xdf6, 0xe16, 0xe36,
                                             0xe56, 0xe76, 0xe96, 0xeb6, 0xed6};
      msr_pmon_box_filter = (unsigned long long[]){
          0xd14, 0xd34, 0xd54, 0xd74, 0xd94, 0xdb4, 0xdd4, 0xdf4,
          0xe14, 0xe34, 0xe54, 0xe74, 0xe94, 0xeb4, 0xed4};
      msr_pmon_ctl0 = (unsigned long long[]){0xd10, 0xd30, 0xd50, 0xd70, 0xd90,
                                             0xdb0, 0xdd0, 0xdf0, 0xe10, 0xe30,
                                             0xe50, 0xe70, 0xe90, 0xeb0, 0xed0};
      msr_pmon_box_ctl = (unsigned long long[]){
          0xd04, 0xd24, 0xd44, 0xd64, 0xd84, 0xda4, 0xdc4, 0xde4,
          0xe04, 0xe24, 0xe44, 0xe64, 0xe84, 0xea4, 0xec4};
      val_box_freeze = 0x30100;
      val_box_reset = 0x30103;
      val_enable_counting = 0x400000;
      val_select_event = 0x401134;
      val_filter = 0x7e0010;
      val_box_unfreeze = 0x30000;
    } else if (!strcmp(archi, "hsw")) {
      max_slices = 18;
      msr_pmon_ctr0 = (unsigned long long[]){
          0xe08, 0xe18, 0xe28, 0xe38, 0xe48, 0xe58, 0xe68, 0xe78, 0xe88,
          0xe98, 0xea8, 0xeb8, 0xec8, 0xed8, 0xee8, 0xef8, 0xf08, 0xf18};
      msr_pmon_box_filter = (unsigned long long[]){
          0xe05, 0xe15, 0xe25, 0xe35, 0xe45, 0xe55, 0xe65, 0xe75, 0xe85,
          0xe95, 0xea5, 0xeb5, 0xec5, 0xed5, 0xee5, 0xef5, 0xf05, 0xf15};
      msr_pmon_ctl0 = (unsigned long long[]){
          0xe01, 0xe11, 0xe21, 0xe31, 0xe41, 0xe51, 0xe61, 0xe71, 0xe81,
          0xe91, 0xea1, 0xeb1, 0xec1, 0xed1, 0xee1, 0xef1, 0xf01, 0xf11};
      msr_pmon_box_ctl = (unsigned long long[]){
          0xe00, 0xe10, 0xe20, 0xe30, 0xe40, 0xe50, 0xe60, 0xe70, 0xe80,
          0xe90, 0xea0, 0xeb0, 0xec0, 0xed0, 0xee0, 0xef0, 0xf00, 0xf10};
      val_box_freeze = 0x30100;
      val_box_reset = 0x30103;
      val_enable_counting = 0x400000;
      val_select_event = 0x401134;
      val_filter = 0x7e0020;
      val_box_unfreeze = 0x30000;
    } else if (!strcmp(archi, "bdw")) {
      max_slices = 24;
      msr_pmon_ctr0 = (unsigned long long[]){
          0xe08, 0xe18, 0xe28, 0xe38, 0xe48, 0xe58, 0xe68, 0xe78,
          0xe88, 0xe98, 0xea8, 0xeb8, 0xec8, 0xed8, 0xee8, 0xef8,
          0xf08, 0xf18, 0xf28, 0xf38, 0xf48, 0xf58, 0xf68, 0xf78};
      msr_pmon_box_filter = (unsigned long long[]){
          0xe05, 0xe15, 0xe25, 0xe35, 0xe45, 0xe55, 0xe65, 0xe75,
          0xe85, 0xe95, 0xea5, 0xeb5, 0xec5, 0xed5, 0xee5, 0xef5,
          0xf05, 0xf15, 0xf25, 0xf35, 0xf45, 0xf55, 0xf65, 0xf75};
      msr_pmon_ctl0 = (unsigned long long[]){
          0xe01, 0xe11, 0xe21, 0xe31, 0xe41, 0xe51, 0xe61, 0xe71,
          0xe81, 0xe91, 0xea1, 0xeb1, 0xec1, 0xed1, 0xee1, 0xef1,
          0xf01, 0xf11, 0xf21, 0xf31, 0xf41, 0xf51, 0xf61, 0xf71};
      msr_pmon_box_ctl = (unsigned long long[]){
          0xe00, 0xe10, 0xe20, 0xe30, 0xe40, 0xe50, 0xe60, 0xe70,
          0xe80, 0xe90, 0xea0, 0xeb0, 0xec0, 0xed0, 0xee0, 0xef0,
          0xf00, 0xf10, 0xf20, 0xf30, 0xf40, 0xf50, 0xf60, 0xf70};
      val_box_freeze = 0x30100;
      val_box_reset = 0x30103;
      val_enable_counting = 0x400000;
      val_select_event = 0x401134;
      val_filter = 0xfe0020;
      val_box_unfreeze = 0x30000;
    }
  }
  // Cores
  else if (!strcmp(class, "core")) {
    max_slices = 4;                                       // FIXME
    if (!strcmp(archi, "skl") || !strcmp(archi, "kbl")) { // >= skylake
      msr_unc_perf_global_ctr = 0xe01;
      val_enable_ctrs = 0x20000000;
    } else {
      msr_unc_perf_global_ctr = 0x391;
      val_enable_ctrs = 0x2000000f;
    }
    msr_unc_cbo_perfevtsel0 =
        (unsigned long long[]){0x700, 0x710, 0x720, 0x730};
    msr_unc_cbo_per_ctr0 = (unsigned long long[]){0x706, 0x716, 0x726, 0x736};
    val_disable_ctrs = 0x0;
    val_select_evt_core = 0x408f34;
    val_reset_ctrs = 0x0;
  }

  if (clflush) {
    printf("Using clflush method\n");
  }

  // Do we scan a few addresses or do we reverse-engineer the function
  if (scan) {
    printf("Scanning a few addresses...\n");
    scan_addresses();
  } else {
    if (!strcmp(class, "core")) {
      // reverse_core();
      reverse_generic();
    } else if (!strcmp(class, "xeon")) {
      reverse_xeon();
    }
  }

  return 0;
}

void scan_addresses() {
  int i;
  static const int nb_addresses = 20;
  char mem[64 * nb_addresses];
  for (i = 0; i < 64 * nb_addresses; i++)
    mem[i] = -1;

  if (clflush) {
    for (i = 0; i < nb_addresses; i++)
      monitor_single_address_clflush((uintptr_t)mem + (i * 64), 1);
  } else if (!strcmp(class, "core")) {
    for (i = 0; i < nb_addresses; i++)
      monitor_single_address_core((uintptr_t)mem + (i * 64), 1);
  } else {
    for (i = 0; i < nb_addresses; i++)
      monitor_single_address_fast((uintptr_t)mem + (i * 64));
  }
}

void reverse_core() {
  register unsigned long long i;
  int j, k;
  unsigned long long offset1, offset2;
  int slice1, slice2;
  int nbits = ceil(log2(nb_cores));
  int oj_a1, oj_a2;
  int w[4][29] = {{0}};

  /*
   * Find the first 21 bits
   */
  // Allocate and initialize a huge page of 2MB
  char *mem = (char *)mmap(
      NULL, HUGE_PAGE_SIZE_2M, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB, -1, 0);
  if (mem == MAP_FAILED) {
    printf("first mmap huge page has failed \n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < HUGE_PAGE_SIZE_2M; i++) {
    mem[i] = 12;
  }

  if (DEBUG) {
    printf("Progress: ");
    fflush(stdout);
  }
  // Find the first 21 bits
  for (i = 0; i < 15; i++) {
    for (j = 0; j < 500; j++) {
      offset1 = j << 6;
      offset2 = offset1 ^ (1 << (i + 6));
      if (clflush) {
        slice1 = monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
        slice2 = monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
      } else {
        slice1 = monitor_single_address_core((uintptr_t)mem + offset1, 0);
        slice2 = monitor_single_address_core((uintptr_t)mem + offset2, 0);
      }
      // for each address bit i of function bit k
      for (k = 0; k < nbits; k++) {
        oj_a1 = (slice1 >> k) & 1; // bit k of output of slice for address 1
        oj_a2 = (slice2 >> k) & 1;
        if (oj_a1 != oj_a2) {
          w[k][i]++;
        }
      }
    }
    if (DEBUG) {
      printf(".");
      fflush(stdout);
    }
  }

  munmap(mem, HUGE_PAGE_SIZE_2M);

  /*
   * Find the other bits, until bit 33
   */

  // Finding the number of free huge pages
  char *cmd = "grep 'HugePages_Free' /proc/meminfo | awk '{print $2}'";
  int hugepages_free = 0;

  char buf[10];
  FILE *fp;

  if ((fp = popen(cmd, "r")) == NULL) {
    printf("Error opening pipe!\n");
    exit(EXIT_FAILURE);
  }

  while (fgets(buf, 10, fp) != NULL) {
    hugepages_free = (int)strtol(buf, NULL, 10);
    // printf("%d\n", hugepages_free);
  }

  if (pclose(fp)) {
    printf("Command not found or exited with error status\n");
    exit(EXIT_FAILURE);
  }

// Mapping memory
#define MMAP_SIZE_CORE (0x200000UL * hugepages_free)
  unsigned long long paddr1, paddr2, candidate;
  unsigned long long mask;
  unsigned long long offset1_i, offset2_i;
  int is_candidate = 0;

  mem = (char *)mmap(NULL, MMAP_SIZE_CORE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                     -1, 0);
  if (mem == MAP_FAILED) {
    printf("second mmap huge page has failed \n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < MMAP_SIZE_CORE; i++) {
    mem[i] = 12;
  }

  // For each bit 21+k -> 33 (bit_max)
  int bit_max = ceil(log2(MMAP_SIZE_CORE));
  for (k = 0; k < bit_max - 21 + 1; k++) {
    is_candidate = 0;
    // Note: shifting by more than 32 bits produce undefined behavior
    // (and opens all sorts of hell)
    // Hence, shifting in two stages
    mask = (unsigned long long)(1 << 21);
    mask = (unsigned long long)(mask << k);

    // Calculate offset1 and offset2
    // such as the addresses differ by one bit only
    for (i = 0; i < hugepages_free; i++) {
      offset1 = i * 0x200000UL;
      paddr1 = (unsigned long long)read_pagemap("/proc/self/pagemap",
                                                (uintptr_t)mem + offset1);
      candidate = paddr1 ^ mask;
      for (j = i; j < hugepages_free; j++) {
        offset2 = j * 0x200000UL;
        paddr2 = (unsigned long long)read_pagemap("/proc/self/pagemap",
                                                  (uintptr_t)mem + offset2);
        if (candidate == paddr2) {
          is_candidate = 1;
          break;
        }
      }
      if (is_candidate) {
        break;
      }
      if (is_candidate == 0 && i == hugepages_free - 1) {
        printf("Not able to test bit %d\n", k + 21);
      }
    }

    // Now, test on 100 different addresses
    for (i = 0; i < 500; i++) {
      offset1_i = offset1 + (i << 6);
      offset2_i = offset2 + (i << 6);
      if (clflush) {
        slice1 = monitor_single_address_clflush((uintptr_t)mem + offset1_i, 0);
        slice2 = monitor_single_address_clflush((uintptr_t)mem + offset2_i, 0);
      } else {
        slice1 = monitor_single_address_core((uintptr_t)mem + offset1_i, 0);
        slice2 = monitor_single_address_core((uintptr_t)mem + offset2_i, 0);
      }
      // for each bit of slice
      for (j = 0; j < nbits; j++) {
        oj_a1 = (slice1 >> j) & 1; // bit j of output of slice for address 1
        oj_a2 = (slice2 >> j) & 1;
        if (oj_a1 != oj_a2) {
          w[j][k + 15] += 1;
        }
      }
    }
    if (DEBUG) {
      printf(".");
      fflush(stdout);
    }
  }

  munmap(mem, MMAP_SIZE_CORE);

  /*
   * Look at the tables to find bits that intervene in the function
   */
  for (j = 0; j < nbits; j++) {
    printf("\no%d =", j);
    for (i = 0; i < 33; i++) {
      if (w[j][i] > 200) { // change constant to avoid noise on the results
        printf(" b%llu", i + 6);
      }
    }
    printf("\n");
  }
}

void reverse_xeon() {
  register unsigned long long i;
  int j, k;
  unsigned long long offset1, offset2;
  int slice1, slice2;
  int nbits = ceil(log2(nb_cores));
  int oj_a1, oj_a2;
  int w[4][29] = {{0}};

  /*
   * Find the first 30 bits
   */
  // Allocate and initialize 1GB
  char *mem = (char *)mmap(
      NULL, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB, -1, 0);
  if (mem == MAP_FAILED) {
    printf("Malloc huge page has failed \n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < HUGE_PAGE_SIZE; i++) {
    mem[i] = 12;
  }

  // Find the first 30th bits
  for (i = 0; i < 24; i++) {
    for (j = 0; j < 100; j++) {
      offset1 = j << 6;
      offset2 = offset1 ^ (1 << (i + 6));
      if (clflush) {
        slice1 = monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
        slice2 = monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
      } else {
        slice1 = monitor_single_address((uintptr_t)mem + offset1);
        slice2 = monitor_single_address((uintptr_t)mem + offset2);
      }
      // for each bit of slice
      for (k = 0; k < nbits; k++) {
        oj_a1 = (slice1 >> k) & 1; // bit k of output of slice for address 1
        oj_a2 = (slice2 >> k) & 1;
        if (oj_a1 != oj_a2) {
          w[k][i]++;
        }
      }
    }
  }

  munmap(mem, HUGE_PAGE_SIZE);

/*
 * Find the other bits, until bit 34
 */
//#define MMAP_SIZE 0x300000000UL
#define NB_PAGES 11
#define MMAP_SIZE 0x40000000UL * NB_PAGES
  unsigned long long paddr1, paddr2, candidate;
  unsigned long long mask;
  unsigned long long offset1_i, offset2_i;
  int is_candidate = 0;

  mem = (char *)mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                     -1, 0);
  if (mem == MAP_FAILED) {
    printf("Malloc huge page has failed \n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < MMAP_SIZE; i++) {
    mem[i] = 12;
  }

  // For each bit 30+k
  for (k = 0; k < 5; k++) {
    is_candidate = 0;
    // Note: shifting by more than 32 bits produce undefined behavior
    // (and opens all sorts of hell)
    // Hence, shifting in two stages
    mask = (unsigned long long)(1 << 30);
    mask = (unsigned long long)(mask << k);

    // Calculate offset1 and offset2
    // such as the addresses differ by one bit only
    for (i = 0; i < NB_PAGES; i++) {
      offset1 = i * 0x40000000UL;
      paddr1 = (unsigned long long)read_pagemap("/proc/self/pagemap",
                                                (uintptr_t)mem + offset1);
      candidate = paddr1 ^ mask;
      for (j = i; j < NB_PAGES; j++) {
        offset2 = j * 0x40000000UL;
        paddr2 = (unsigned long long)read_pagemap("/proc/self/pagemap",
                                                  (uintptr_t)mem + offset2);
        if (candidate == paddr2) {
          is_candidate = 1;
          break;
        }
      }
      if (is_candidate) {
        break;
      }
      if (is_candidate == 0 && i == NB_PAGES - 1) {
        printf("Not able to test bit %d\n", k + 30);
      }
    }

    // Now, test on 100 different addresses
    for (i = 0; i < 100; i++) {
      offset1_i = offset1 + (i << 6);
      offset2_i = offset2 + (i << 6);
      if (clflush) {
        slice1 = monitor_single_address_clflush((uintptr_t)mem + offset1_i, 0);
        slice2 = monitor_single_address_clflush((uintptr_t)mem + offset2_i, 0);
      } else {
        slice1 = monitor_single_address((uintptr_t)mem + offset1_i);
        slice2 = monitor_single_address((uintptr_t)mem + offset2_i);
      }

      // for each bit of slice
      for (j = 0; j < nbits; j++) {
        oj_a1 = (slice1 >> j) & 1; // bit j of output of slice for address 1
        oj_a2 = (slice2 >> j) & 1;
        if (oj_a1 != oj_a2) {
          w[j][k + 24] += 1;
        }
      }
    }
  }

  munmap(mem, MMAP_SIZE);

  /*
   * Look at the tables to find bits that intervene in the function
   */
  for (j = 0; j < nbits; j++) {
    printf("\no%d =", j);
    for (i = 0; i < 29; i++) {
      if (w[j][i] > 10) {
        printf(" b%llu", i + 6);
      }
    }
    printf("\n");
  }
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

void reverse_generic() {
  register unsigned long long i;
  int j, k;
  unsigned long long offset1, offset2;
  int slice1, slice2;
  int nbits = ceil(log2(nb_cores));
  int oj_a1, oj_a2;
  int w[4][40] = {{0}};

  /*
   * Find the first 21 bits
   */
  // Allocate and initialize a huge page of 2MB
  char *mem = (char *)mmap(
      NULL, HUGE_PAGE_SIZE_2M, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB, -1, 0);
  if (mem == MAP_FAILED) {
    printf("first mmap huge page has failed \n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < HUGE_PAGE_SIZE_2M; i++) {
    mem[i] = 12;
  }

  if (DEBUG) {
    printf("Progress: ");
    fflush(stdout);
  }
  // Find the first 21 bits
  for (i = 0; i < 15; i++) {
    for (j = 0; j < ADDR_PER_BIT; j++) {
      offset1 = j << 6;
      offset2 = offset1 ^ (1 << (i + 6));
      if (clflush) {
        slice1 = monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
        slice2 = monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
      } else if (!strcmp(class, "core")) {
        slice1 = monitor_single_address_core((uintptr_t)mem + offset1, 0);
        slice2 = monitor_single_address_core((uintptr_t)mem + offset2, 0);
      } else {
        slice1 = monitor_single_address((uintptr_t)mem + offset1);
        slice2 = monitor_single_address((uintptr_t)mem + offset2);
      }
      // for each address bit i of function bit k
      for (k = 0; k < nbits; k++) {
        oj_a1 = (slice1 >> k) & 1; // bit k of output of slice for address 1
        oj_a2 = (slice2 >> k) & 1;
        if (oj_a1 != oj_a2) {
          w[k][i]++;
        }
      }
    }
    if (DEBUG) {
      printf(".");
      fflush(stdout);
    }
  }

  munmap(mem, HUGE_PAGE_SIZE_2M);

  /*
   * Find the other bits, until bit 33
   */

  // Finding the number of free huge pages
  char *cmd = "grep 'HugePages_Free' /proc/meminfo | awk '{print $2}'";
  int hugepages_free = 0;

  char buf[10];
  FILE *fp;

  if ((fp = popen(cmd, "r")) == NULL) {
    printf("Error opening pipe!\n");
    exit(EXIT_FAILURE);
  }

  while (fgets(buf, 10, fp) != NULL) {
    hugepages_free = (int)strtol(buf, NULL, 10);
  }

  if (pclose(fp)) {
    printf("Command not found or exited with error status\n");
    exit(EXIT_FAILURE);
  }

// Mapping memory
#define MMAP_SIZE_CORE (0x200000UL * hugepages_free)
  int is_candidate = 0;

  mem = (char *)mmap(NULL, MMAP_SIZE_CORE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                     -1, 0);
  if (mem == MAP_FAILED) {
    printf("second mmap huge page has failed \n");
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < MMAP_SIZE_CORE; i++) {
    mem[i] = 12;
  }

  // Reverse mapping
  int bit_max = ceil(log2(MMAP_SIZE_CORE));
  int mapping_size = pow(2, bit_max - 21 + 2);
  unsigned long long *rev_map =
      (unsigned long long *)malloc(mapping_size * sizeof(unsigned long long));
  for (i = 0; i < mapping_size; i++) {
    rev_map[i] = -1;
  }

  for (i = 0; i < hugepages_free; i++) {
    unsigned long long offset = i * 0x200000UL;
    unsigned long long paddr = (unsigned long long)read_pagemap(
        "/proc/self/pagemap", (uintptr_t)mem + offset);
    unsigned long long ppn = (paddr & ~(~0 << (bit_max - 21 + 1)) << 21) >>
                             21; // keep bits 21 to bit_max+1 from the address
    if (paddr == 0)
      printf(" %llu", i);
    rev_map[ppn] = i;
  }

  // For each bit 21+k -> bit_max
  for (i = 0; i < bit_max - 21 + 1; i++) {
    is_candidate = 0; // is there a 2MB page candidate
    unsigned long long ppn1, ppn2;

    // trying all addresses until finding a pair that is valid
    for (j = 0; j < mapping_size; j++) {
      ppn1 = j;               // offset in rev_map
      ppn2 = ppn1 ^ (1 << i); // offset in rev_map
      if (rev_map[ppn1] != -1 && rev_map[ppn2] != -1) {
        is_candidate = 1;
        break;
      }
    }

    // continue with next bit if there is no valid pair
    if (is_candidate == 0) {
      printf("\nNot able to test bit %lld", i + 21);
      continue;
    }

    // Test for several addresses
    for (j = 0; j < ADDR_PER_BIT; j++) {
      offset1 = (rev_map[ppn1] << 21) + j;
      offset2 = (rev_map[ppn2] << 21) + j;
      if (clflush) {
        slice1 = monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
        slice2 = monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
      } else if (!strcmp(class, "core")) {
        slice1 = monitor_single_address_core((uintptr_t)mem + offset1, 0);
        slice2 = monitor_single_address_core((uintptr_t)mem + offset2, 0);
      } else {
        slice1 = monitor_single_address((uintptr_t)mem + offset1);
        slice2 = monitor_single_address((uintptr_t)mem + offset2);
      }
      // for each address bit i of function bit k
      for (k = 0; k < nbits; k++) {
        oj_a1 = (slice1 >> k) & 1; // bit k of output of slice for address 1
        oj_a2 = (slice2 >> k) & 1;
        if (oj_a1 != oj_a2) {
          w[k][i + 15]++;
        }
      }
    }

    if (DEBUG) {
      printf(".");
      fflush(stdout);
    }
  }

  munmap(mem, MMAP_SIZE_CORE);

  /*
   * Look at the tables to find bits that intervene in the function
   */
  printf("\n");
  for (j = 0; j < nbits; j++) {
    printf("\no%d =", j);
    for (i = 0; i < bit_max; i++) {
      if (w[j][i] > THRESHOLD) {
        printf(" b%llu", i + 6);
      }
    }
    printf("\n");
  }
}
