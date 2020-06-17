#define _GNU_SOURCE
#include <cpuid.h>
#include <getopt.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "global_variables.h"
#include "monitoring.h"
#include "poke.h"
#include "rdmsr.h"
#include "scan.h"
#include "util.h"
#include "wrmsr.h"

#define HUGE_PAGE_SIZE (1 * 1024 * 1024 * 1024)

void print_help() {
  fprintf(stderr,
          "  >> Usage: sudo ./scan -c nb_cores -a (sdb|ivb|hsw|core)\n");
}

/*
 * Declare architecture- and core-dependent variables
 */

char *archi;
int nb_cores;
int max_slices;

// Xeons MSRs and values
unsigned long long *msr_pmon_ctr0;
unsigned long long *msr_pmon_box_filter;
unsigned long long *msr_pmon_ctl0;
unsigned long long *msr_pmon_box_ctl;
unsigned long long val_box_freeze;
unsigned long long val_box_reset;
unsigned long long val_enable_counting;
unsigned long long val_select_event;
unsigned long long val_filter;
unsigned long long val_box_unfreeze;

// Core MSRs and values
unsigned long long msr_unc_perf_global_ctr;
unsigned long long *msr_unc_cbo_perfevtsel0;
unsigned long long *msr_unc_cbo_per_ctr0;
unsigned long long val_enable_ctrs;
unsigned long long val_disable_ctrs;
unsigned long long val_select_evt_core;
unsigned long long val_reset_ctrs;

int main(int argc, char **argv) {

  /*
   * Pin to core
   */
  int mask = 0;
  cpu_set_t my_set;
  CPU_ZERO(&my_set);
  CPU_SET(mask, &my_set);
  sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
  printf(" CPU: Mask %d; ", mask);
  print_cpu();

  /*
   * Options
   */
  char *archi;
  int opt, a, c;
  a = c = 0;

  while ((opt = getopt(argc, argv, "c:a:h")) != -1) {
    switch (opt) {
    case 'h':
      print_help();
      exit(1);
    case 'c':
      nb_cores = atoi(optarg);
      c = 1;
      break;
    case 'a':
      archi = optarg;
      a = 1;
      break;
    default:
      print_help();
      exit(1);
    }
  }

  if (!c || !a) {
    print_help();
    exit(1);
  }

  /*
   * Initialize architecture-dependent variables
   */

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
    // val_select_event = 0x400334; // data_read
    // val_select_event = 0x400534; // write
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
        0xe01, 0xe11, 0xe21, 0xe31, 0xe41, 0xe51, 0xe61, 0xe71, 0xe81, 0xe91,
        0xea1, 0xeb1, 0xec1, 0xec1, 0xed1, 0xee1, 0xef1, 0xf01, 0xf11};
    msr_pmon_box_ctl = (unsigned long long[]){
        0xe00, 0xe10, 0xe20, 0xe30, 0xe40, 0xe50, 0xe60, 0xe70, 0xe80,
        0xe90, 0xea0, 0xeb0, 0xec0, 0xed0, 0xee0, 0xef0, 0xf00, 0xf10};
    val_box_freeze = 0x30100;
    val_box_reset = 0x30103;
    val_enable_counting = 0x400000;
    val_select_event = 0x401134;
    val_filter = 0x7e0020;
    val_box_unfreeze = 0x30000;
  } else if (!strcmp(archi, "core")) {

    if (get_cpu_architecture() >= 0x16) {
      // >= skylake
      msr_unc_perf_global_ctr = 0xe01;
      val_enable_ctrs = 0x20000000;
    } else {
      msr_unc_perf_global_ctr = 0x391;
      val_enable_ctrs = 0x2000000f;
    }

    max_slices = 4;
    msr_unc_cbo_perfevtsel0 =
        (unsigned long long[]){0x700, 0x710, 0x720, 0x730};
    msr_unc_cbo_per_ctr0 = (unsigned long long[]){0x706, 0x716, 0x726, 0x736};
    val_disable_ctrs = 0x0;
    val_select_evt_core = 0x408f34;
    val_reset_ctrs = 0x0;
  } else if (!strcmp(archi, "clflush")) {
    max_slices = 18;
    // Nothing more to initialize
  } else {
    fprintf(stderr, "  >> Micro-architectures supported: sdb (Xeon Sandy \
       Bridge), ivb (Xeon Ivy Bridge), hsw (Xeon Haswell), core (Core ix-xxxx)\n");
    exit(1);
  }

  /*
   * Verify number of cores is coherent with micro-architecture
   */
  if (nb_cores > max_slices) {
    fprintf(stderr,
            "Specified number of cores (%d) incoherent with maximum number of "
            "core of specified micro-architecure (%d for %s). \n",
            nb_cores, max_slices, archi);
    print_help();
    exit(1);
  }

  printf("Micro-architecture: %s\n", archi);
  printf("Number of cores: %d\n", nb_cores);

  /*
   * Allocate and initialize memory for monitoring addresses
   */

  unsigned long long i;
  unsigned long long const stride = 64;
  unsigned long long const nb_loops = 100;

  // For 4kB pages
  char *mem = (char *)malloc(nb_loops * stride);
  if (mem == NULL) {
    printf("Malloc has failed \n");
    return -1;
  } else {
    printf("[+] Allocated memory\n");
  }

  // For huge pages ()
  /*char * mem = (char*)mmap(NULL, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
  MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB, - 1, 0); if (mem ==
  NULL) { printf("Malloc huge page has failed \n"); } else { printf("[+] Yay,
  allocated 1GB\n");
  }*/

  for (i = 0; i < nb_loops * stride; i++) {
    mem[i] = 12;
  }

  printf("[+] Initialized memory\n");

  /*
   * Monitor addresses
   */
  if (!strcmp(archi, "clflush")) {
    for (i = 0; i < nb_loops; i++) {
      monitor_single_address_clflush((uintptr_t)mem + (i * stride), 1);
    }
  } else if (!strcmp(archi, "core")) {
    for (i = 0; i < nb_loops; i++) {
      monitor_single_address_core((uintptr_t)mem + (i * stride), 1);
    }
  } else {
    for (i = 0; i < nb_loops; i++) {
      monitor_single_address_fast((uintptr_t)mem + (i * stride));
    }
  }

  // munmap(mem, HUGE_PAGE_SIZE);
  free(mem);

  return 0;
}
