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

#include "arch.h"
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

int verbose = 0;
int clflush = 0;

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
    while ((opt = getopt(argc, argv, "hfv")) != -1) {
        switch (opt) {
        case 'h':
            print_help();
            exit(1);
        case 'f':
            clflush = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            print_help();
            exit(1);
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
    printf("Micro-architecture: %s\n", uarch_names[archi]);
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
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB, - 1, 0); if (mem
    == NULL) { printf("Malloc huge page has failed \n"); } else { printf("[+]
    Yay, allocated 1GB\n");
    }*/

    for (i = 0; i < nb_loops * stride; i++) {
        mem[i] = 12;
    }

    printf("[+] Initialized memory\n");

    /*
     * Monitor addresses
     */
    if (clflush) {
        for (i = 0; i < nb_loops; i++) {
            monitor_single_address_clflush((uintptr_t)mem + (i * stride), 1);
        }
    } else if (class == INTEL_CORE) {
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
