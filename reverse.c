/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2016 Clémentine Maurice
 *   Copyright 2021 Guillaume Didier
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 *   Boston MA 02110-1301, USA; either version 2 of the License, or
 *   (at your option) any later version. *    
 *    
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *   
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * ----------------------------------------------------------------------- */


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

#include "arch.h"
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

int clflush = 0;
int scan = 0;
int verbose = 0;

int main(int argc, char **argv) {

    /*
     * Verify CPU is Intel
     */
    if (!is_intel()) {
        fprintf(stderr,"CPU is not Intel\n");
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


    /*
     * Options
     */
    int opt;

    static struct option long_options[] = {{"help", no_argument, NULL, 'h'},
                                           {"clflush", no_argument, NULL, 'f'},
                                           {"scan", no_argument, NULL, 's'},
                                           {"verbose", no_argument, NULL, 'v'},
                                           {NULL, 0, NULL, 0}};

    while ((opt = getopt_long(argc, argv, "hfsv", long_options, NULL)) != -1) {
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
        case 'v':
            verbose = 1;
            break;
        default:
            print_help();
            exit(0);
        }
    }

    if (determine_class_uarch(cpu_model) < 0 && !clflush) {
        exit(EXIT_FAILURE);
    }
    /*
     * Initialize architecture-dependent variables
     */

    if (setup_perf_counters(class, archi, nb_cores) < 0 && !clflush) {
        exit(EXIT_FAILURE);
    }

    if (clflush) {
        if (verbose) {
            printf("Using clflush method\n");
        }
        max_slices = 64; // A large number given there are no limits
    }

    /*
     * Verify number of cores is coherent with micro-architecture
     */
    if (nb_cores > max_slices) {
        fprintf(
            stderr,
            "Specified number of cores (%d) incoherent with maximum number of "
            "core of specified micro-architecure (%d for %s). \n",
            nb_cores, max_slices, uarch_names[archi]);
        print_help();
        exit(1);
    }

    // Do we scan a few addresses or do we reverse-engineer the function
    if (scan) {
        if (verbose) {
            printf("Scanning a few addresses...\n");
        }
        scan_addresses();
    } else {
        if (class == INTEL_CORE) {
            // reverse_core();
            reverse_generic();
        } else if (class == INTEL_XEON) {
            reverse_xeon();
        } else {
            fprintf(stderr,"Unsupported");
            exit(EXIT_FAILURE);
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
    } else if (class == INTEL_CORE) {
        if(verbose) {
            printf("monitoring core\n");
        }
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

#if DEBUG
    fprintf(stderr, "Progress: ");
#endif // DEBUG
    // Find the first 21 bits
    for (i = 0; i < 15; i++) {
        for (j = 0; j < 500; j++) {
            offset1 = j << 6;
            offset2 = offset1 ^ (1 << (i + 6));
            if (clflush) {
                slice1 =
                    monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
                slice2 =
                    monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
            } else {
                slice1 =
                    monitor_single_address_core((uintptr_t)mem + offset1, 0);
                slice2 =
                    monitor_single_address_core((uintptr_t)mem + offset2, 0);
            }
            // for each address bit i of function bit k
            for (k = 0; k < nbits; k++) {
                oj_a1 =
                    (slice1 >> k) & 1; // bit k of output of slice for address 1
                oj_a2 = (slice2 >> k) & 1;
                if (oj_a1 != oj_a2) {
                    w[k][i]++;
                }
            }
        }
#if DEBUG
        fprintf(stderr,".");
#endif // DEBUG
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
        fprintf(stderr,"Error opening pipe!\n");
        exit(EXIT_FAILURE);
    }

    while (fgets(buf, 10, fp) != NULL) {
        hugepages_free = (int)strtol(buf, NULL, 10);
        // printf("%d\n", hugepages_free);
    }

    if (pclose(fp)) {
        fprintf(stderr,"Command not found or exited with error status\n");
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
        fprintf(stderr,"second mmap huge page has failed \n");
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
                paddr2 = (unsigned long long)read_pagemap(
                    "/proc/self/pagemap", (uintptr_t)mem + offset2);
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
                slice1 = monitor_single_address_clflush(
                    (uintptr_t)mem + offset1_i, 0);
                slice2 = monitor_single_address_clflush(
                    (uintptr_t)mem + offset2_i, 0);
            } else {
                slice1 =
                    monitor_single_address_core((uintptr_t)mem + offset1_i, 0);
                slice2 =
                    monitor_single_address_core((uintptr_t)mem + offset2_i, 0);
            }
            // for each bit of slice
            for (j = 0; j < nbits; j++) {
                oj_a1 =
                    (slice1 >> j) & 1; // bit j of output of slice for address 1
                oj_a2 = (slice2 >> j) & 1;
                if (oj_a1 != oj_a2) {
                    w[j][k + 15] += 1;
                }
            }
        }
#if DEBUG
            fprintf(stderr,".");
#endif // DEBUG
    }

    munmap(mem, MMAP_SIZE_CORE);

    /*
     * Look at the tables to find bits that intervene in the function
     */
    for (j = 0; j < nbits; j++) {
        printf("\no%d =", j);
        for (i = 0; i < 33; i++) {
            if (w[j][i] >
                200) { // change constant to avoid noise on the results
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
        fprintf(stderr,"Malloc huge page has failed \n");
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
                slice1 =
                    monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
                slice2 =
                    monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
            } else {
                slice1 = monitor_single_address((uintptr_t)mem + offset1);
                slice2 = monitor_single_address((uintptr_t)mem + offset2);
            }
            // for each bit of slice
            for (k = 0; k < nbits; k++) {
                oj_a1 =
                    (slice1 >> k) & 1; // bit k of output of slice for address 1
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
#define MMAP_SIZE (0x40000000UL * NB_PAGES)
    unsigned long long paddr1, paddr2, candidate;
    unsigned long long mask;
    unsigned long long offset1_i, offset2_i;
    int is_candidate = 0;

    mem = (char *)mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                       -1, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "Malloc huge page has failed \n");
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
                paddr2 = (unsigned long long)read_pagemap(
                    "/proc/self/pagemap", (uintptr_t)mem + offset2);
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
                slice1 = monitor_single_address_clflush(
                    (uintptr_t)mem + offset1_i, 0);
                slice2 = monitor_single_address_clflush(
                    (uintptr_t)mem + offset2_i, 0);
            } else {
                slice1 = monitor_single_address((uintptr_t)mem + offset1_i);
                slice2 = monitor_single_address((uintptr_t)mem + offset2_i);
            }

            // for each bit of slice
            for (j = 0; j < nbits; j++) {
                oj_a1 =
                    (slice1 >> j) & 1; // bit j of output of slice for address 1
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
        fprintf(stderr,"first mmap huge page has failed \n");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < HUGE_PAGE_SIZE_2M; i++) {
        mem[i] = 12;
    }

#if DEBUG
    fprintf(stderr, "Progress: ");
#endif // DEBUG
    // Find the first 21 bits
    for (i = 0; i < 15; i++) {
        if(verbose) {
            printf("Bit %lld\n", i + 6);
        }
        for (j = 0; j < ADDR_PER_BIT; j++) {
            offset1 = j << 6;
            offset2 = offset1 ^ (1 << (i + 6));
            if(verbose) {
                printf("Comparing %p and %p:", mem + offset1, mem + offset2);
            }
            if (clflush) {
                slice1 =
                    monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
                slice2 =
                    monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
            } else if (class == INTEL_CORE) {
                slice1 =
                    monitor_single_address_core((uintptr_t)mem + offset1, 0);
                slice2 =
                    monitor_single_address_core((uintptr_t)mem + offset2, 0);
            } else {
                slice1 = monitor_single_address((uintptr_t)mem + offset1);
                slice2 = monitor_single_address((uintptr_t)mem + offset2);
            }
            if(verbose) {
                printf("Slice1 %d, Slice2 %d\n", slice1, slice2);
            }
            // for each address bit i of function bit k
            for (k = 0; k < nbits; k++) {
                oj_a1 =
                    (slice1 >> k) & 1; // bit k of output of slice for address 1
                oj_a2 = (slice2 >> k) & 1;
                if (oj_a1 != oj_a2) {
                    if(verbose) {
                        printf("Found address bit %lld used for hash bit %d\n", i,
                               k);
                    }
                    w[k][i]++;
                }
            }
        }
        if(verbose) {
            printf("For addr bit %lld: ", i + 6);

            for (k = 0; k < nbits; k++) {
                printf("hash bit %d: %d samples, ", k, w[k][i]);
            }
            printf("\n");
        }
#if DEBUG
        fprintf(stderr, ".");
#endif // DEBUG
    }
    if(verbose) {
        printf("Done bit up to %lld\n", i + 6);
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
        fprintf(stderr, "Error opening pipe!\n");
        exit(EXIT_FAILURE);
    }

    while (fgets(buf, 10, fp) != NULL) {
        hugepages_free = (int)strtol(buf, NULL, 10);
    }

    if (pclose(fp)) {
        fprintf(stderr, "Command not found or exited with error status\n");
        exit(EXIT_FAILURE);
    }

// Mapping memory
#define MMAP_SIZE_CORE (0x200000UL * hugepages_free)
    int is_candidate = 0;

    mem = (char *)mmap(NULL, MMAP_SIZE_CORE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                       -1, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "second mmap huge page has failed \n");
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
        unsigned long long ppn =
            (paddr & ~(~0 << (bit_max - 21 + 1)) << 21) >>
            21; // keep bits 21 to bit_max+1 from the address
        if (paddr == 0)
            if(verbose) {
                printf(" %llu", i);
            }
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
                slice1 =
                    monitor_single_address_clflush((uintptr_t)mem + offset1, 0);
                slice2 =
                    monitor_single_address_clflush((uintptr_t)mem + offset2, 0);
            } else if (class == INTEL_CORE) {
                slice1 =
                    monitor_single_address_core((uintptr_t)mem + offset1, 0);
                slice2 =
                    monitor_single_address_core((uintptr_t)mem + offset2, 0);
            } else {
                slice1 = monitor_single_address((uintptr_t)mem + offset1);
                slice2 = monitor_single_address((uintptr_t)mem + offset2);
            }
            // for each address bit i of function bit k
            for (k = 0; k < nbits; k++) {
                oj_a1 =
                    (slice1 >> k) & 1; // bit k of output of slice for address 1
                oj_a2 = (slice2 >> k) & 1;
                if (oj_a1 != oj_a2) {
                    w[k][i + 15]++;
                }
            }
        }
        if(verbose) {
            printf("For addr bit %lld: ", i + 6 + 15);
            for (k = 0; k < nbits; k++) {
                printf("hash bit %d: %d samples, ", k, w[k][i]);
            }
            printf("\n");
        }

#if DEBUG
        fprintf(stderr, ".");
#endif // DEBUG
    }

    munmap(mem, MMAP_SIZE_CORE);

    /*
     * Look at the tables to find bits that intervene in the function
     */
    fprintf(stderr, "\n");
    for (j = 0; j < nbits; j++) {
        fprintf(stderr, "\no%d =", j);
        printf("\no%d =", j);
        for (i = 0; i < bit_max; i++) {
            if (w[j][i] > THRESHOLD) {
                fprintf(stderr, " b%llu", i + 6);
                printf(" b%llu", i + 6);
            }
        }
        fprintf(stderr, "\n");
        printf("\n");
    }
}
