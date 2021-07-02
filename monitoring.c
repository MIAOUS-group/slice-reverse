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

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arch.h"
#include "global_variables.h"
#include "monitoring.h"
#include "poke.h"
#include "rdmsr.h"
#include "util.h"
#include "wrmsr.h"

#define SIZE_HIST (600)

int monitor_single_address_clflush(uintptr_t addr, int print) {
    uintptr_t paddr = read_pagemap("/proc/self/pagemap", addr);

    unsigned long cores_package = cores_per_package();

    int *map_coreid = mapping_coreid();
    int *map_apicid = mapping_apicid();
    int i, j, thread;

    size_t hit_histogram[SIZE_HIST];
    int nb_tries = 50 * 1024;

    /*
     * Execute some code on every core (!= every thread)
     */

    int *core_used;
    core_used = (int *)calloc(cores_package, sizeof(int));
    unsigned long current_apicid = -1;
    int current_core = -1;

    int mask;
    int n = threads_per_package();
    int slice = 0;

    for (thread = 0; thread < n; thread++) {
        mask = thread;
        cpu_set_t my_set;  // Define your cpu_set bit mask.
        CPU_ZERO(&my_set); // Initialize it all to 0, i.e. no CPUs selected.
        CPU_SET(mask, &my_set); // set the bit that represents core
        // Set affinity of this process to mask
        if (sched_setaffinity(0, sizeof(cpu_set_t), &my_set) == -1) {
            printf("Error with sched_setaffinity\n");
            exit(-1);
        }

        current_apicid = current_apic();
        current_core = apicid2coreid(current_apicid, map_apicid, map_coreid);

        memset(hit_histogram, 0, SIZE_HIST * sizeof(*hit_histogram));

        if (core_used[current_core] == 0)
        // Code to execute in every core
        {
            // Construct clflush hit histogram
            for (i = 0; i < nb_tries; ++i) {
                size_t d = flush_hit((char *)addr);
                hit_histogram[MIN(599, d)]++;
                for (j = 0; j < 1; ++j)
                    sched_yield();
            }

// Print histogram for each core if not sure of what the threshold values should
// be
//#define DEBUG
#ifdef DEBUG
            for (i = 145; i < 180; ++i) {
                printf("%3d: %15zu\n", i, hit_histogram[i]);
            }
#endif

            // Based on the historgram, is the address on the same slice as the
            // executing core?
            if (same_slice(hit_histogram)) {
                slice = current_core;
            }

            // Current core has been used (do not remove)
            core_used[current_core] = 1;
        }
    }

    // Pretty print
    if (print) {
        print_bin(paddr);
        printf(" %d\n", slice);
    }

    return slice;
}

int monitor_single_address_core(uintptr_t addr, int print) {
    int i;

    // Disable counters
    uint64_t val[] = {val_disable_ctrs};
    wrmsr_on_cpu_0(msr_unc_perf_global_ctr, 1, val);

    // Reset counters
    val[0] = val_reset_ctrs;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_unc_cbo_per_ctr0[i], 1, val);
    }

    // Select event to monitor
    val[0] = val_select_evt_core;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_unc_cbo_perfevtsel0[i], 1, val);
    }

    // Enable counting
    val[0] = val_enable_ctrs;
    // val[0] = val_enable_ctrs;
    wrmsr_on_cpu_0(msr_unc_perf_global_ctr, 1, val);

    // Launch program to monitor
    uintptr_t paddr;
    paddr = poke(addr);

    /*
      // Disable counters
      val[0] = val_disable_ctrs;
      wrmsr_on_cpu_0(msr_unc_perf_global_ctr,1,val);
    */

    // Read counter
    int *cboxes = calloc(max_slices, sizeof(int));
    int *cboxes_tri = calloc(max_slices, sizeof(int));
    int res_temp;
    for (i = 0; i < nb_cores; i++) {
        res_temp = rdmsr_on_cpu_0(msr_unc_cbo_per_ctr0[i]);
        cboxes[i] = ((res_temp - nb_pokes) < 0) ? 0 : res_temp - nb_pokes;
        cboxes_tri[i] = ((res_temp - nb_pokes) < 0) ? 0 : res_temp - nb_pokes;
    }

    int slice = 0;
    int first = 0;
    int second = 0;
    float percent;

    // Interpreting the results
    //

    // Finding the slice in which the address is
    for (i = 0; i < max_slices; i++) {
        if (cboxes[i] > cboxes[slice]) {
            slice = i;
        }
    }

    // Calculate the ratio between the first and the second result
    // to estimate the error
    quicksort(cboxes_tri, 0, max_slices - 1);
    first = cboxes_tri[max_slices - 1];
    second = cboxes_tri[max_slices - 2];
    percent = ((float)second) / ((float)first) * 100;

    // Pretty print
    if (print) {
        print_bin(paddr);
        printf(" %d %6.2f", slice, percent);
        for (i = 0; i < nb_cores; i++) {
            printf(" % 6d", cboxes[i]);
        }
        printf("\n");
    }

    free(cboxes);
    free(cboxes_tri);

    return slice;
}

int monitor_single_address_fast(uintptr_t addr) {
    // Session monitoring
    //
    // The whole setup is explained in the section 2.1.2 of the manual (p15)
    // Beware: it is written to reset all counters after enabling monitoring and
    // selecting event to monitor, while the reset should be done before

    int i;

    // Freeze box counters

    uint64_t val[] = {val_box_freeze};

    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctr0[i], 1, val);
    }

    // Reset counters
    val[0] = val_box_reset;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Enable counting
    val[0] = val_enable_counting;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctl0[i], 1, val);
    }

    // Select event to monitor: umask and filter
    val[0] = val_select_event;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctl0[i], 1, val);
    }
    val[0] = val_filter;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_filter[i], 1, val);
    }

    // Unfreezing box counters
    val[0] = val_box_unfreeze;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Launch program to monitor
    uintptr_t paddr;
    paddr = poke(addr);

    // Freeze box counters
    val[0] = val_box_freeze;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Read counters
    //
    int *cboxes = calloc(max_slices, sizeof(int));
    int *cboxes_tri = calloc(max_slices, sizeof(int));
    int res_temp;
    for (i = 0; i < nb_cores; i++) {
        res_temp = rdmsr_on_cpu_0(msr_pmon_ctr0[i]);
        cboxes[i] = ((res_temp - nb_pokes) < 0) ? 0 : res_temp - nb_pokes;
        cboxes_tri[i] = ((res_temp - nb_pokes) < 0) ? 0 : res_temp - nb_pokes;
    }

    int slice = 0;
    int first = 0;
    int second = 0;
    float percent;

    // Interpreting the results
    //

    // Finding the slice in which the address is
    for (i = 0; i < max_slices; i++) {
        if (cboxes[i] > cboxes[slice]) {
            slice = i;
        }
    }

    // Calculate the ratio between the first and the second result
    // to estimate the error
    quicksort(cboxes_tri, 0, max_slices - 1);
    first = cboxes_tri[max_slices - 1];
    second = cboxes_tri[max_slices - 2];
    percent = ((float)second) / ((float)first) * 100;

    // Pretty print
    print_bin(paddr);
    printf(" %d %6.2f", slice, percent);
    for (i = 0; i < nb_cores; i++) {
        printf(" % 6d", cboxes[i]);
    }
    printf("\n");

    free(cboxes);
    free(cboxes_tri);

    return slice;
}

int monitor_single_address_print(uintptr_t addr) {
    // Session monitoring
    //
    // The whole setup is explained in the section 2.1.2 of the manual (p15)
    // Beware: it is written to reset all counters after enabling monitoring and
    // selecting event to monitor, while the reset should be done before

    int i;

    // Freeze box counters
    uint64_t val[] = {val_box_freeze};
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctr0[i], 1, val);
    }

    // Reset counters
    val[0] = val_box_reset;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Enable counting
    val[0] = val_enable_counting;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctl0[i], 1, val);
    }

    // Select event to monitor: umask and filter
    val[0] = val_select_event;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctl0[i], 1, val);
    }
    val[0] = val_filter;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_filter[i], 1, val);
    }

    // Unfreezing box counters
    val[0] = val_box_unfreeze;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Launch program to monitor
    uintptr_t paddr;
    paddr = poke(addr);

    // Freeze box counters
    val[0] = val_box_freeze;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Read counters
    //
    int *cboxes = calloc(max_slices, sizeof(int));
    int *cboxes_tri = calloc(max_slices, sizeof(int));
    for (i = 0; i < nb_cores; i++) {
        cboxes[i] = rdmsr_on_cpu_0(msr_pmon_ctr0[i]);
        cboxes_tri[i] = rdmsr_on_cpu_0(msr_pmon_ctr0[i]);
    }

    int slice = 0;
    int first = 0;
    int second = 0;
    float percent;

    // Interpreting the results
    //

    // Finding the slice in which the address is
    for (i = 0; i < max_slices; i++) {
        if (cboxes[i] > cboxes[slice]) {
            slice = i;
        }
    }

    // Calculate the ratio between the first and the second result
    // to estimate the error
    quicksort(cboxes_tri, 0, max_slices - 1);
    first = cboxes_tri[max_slices - 1];
    second = cboxes_tri[max_slices - 2];
    percent = ((float)second) / ((float)first) * 100;

    // Pretty print
    print_bin(paddr);
    printf(" %d %6.2f", slice, percent);
    for (i = 0; i < nb_cores; i++) {
        printf(" % 6d", cboxes[i]);
    }
    printf("\n");

    free(cboxes);
    free(cboxes_tri);

    return slice;
}

int monitor_single_address(uintptr_t addr) {
    // Session monitoring
    //
    // The whole setup is explained in the section 2.1.2 of the manual (p15)
    // Beware: it is written to reset all counters after enabling monitoring and
    // selecting event to monitor, while the reset should be done before

    int i;

    // Freeze box counters
    uint64_t val[] = {val_box_freeze};
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctr0[i], 1, val);
    }

    // Reset counters
    val[0] = val_box_reset;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Enable counting
    val[0] = val_enable_counting;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctl0[i], 1, val);
    }

    // Select event to monitor: umask and filter
    val[0] = val_select_event;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_ctl0[i], 1, val);
    }
    val[0] = val_filter;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_filter[i], 1, val);
    }

    // Unfreezing box counters
    val[0] = val_box_unfreeze;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Launch program to monitor
    poke(addr);

    // Freeze box counters
    val[0] = val_box_freeze;
    for (i = 0; i < nb_cores; i++) {
        wrmsr_on_cpu_0(msr_pmon_box_ctl[i], 1, val);
    }

    // Read counters
    //
    int *cboxes = calloc(max_slices, sizeof(int));
    int *cboxes_tri = calloc(max_slices, sizeof(int));
    for (i = 0; i < nb_cores; i++) {
        cboxes[i] = rdmsr_on_cpu_0(msr_pmon_ctr0[i]);
        cboxes_tri[i] = rdmsr_on_cpu_0(msr_pmon_ctr0[i]);
    }

    // Interpreting the results
    //

    // Finding the slice in which the address is
    int slice = 0;
    for (i = 0; i < max_slices; i++) {
        if (cboxes[i] > cboxes[slice]) {
            slice = i;
        }
    }

    free(cboxes);
    free(cboxes_tri);

    return slice;
}
