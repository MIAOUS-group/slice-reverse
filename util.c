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
#include "cpuid.h"
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define PAGEMAP_ENTRY 8
#define GET_BIT(X, Y) (X & ((uint64_t)1 << Y)) >> Y
#define GET_PFN(X) (X & 0x7FFFFFFFFFFFFF)

// For some reason, the values returned by the calibration tool don't work
// To have meaningful threshold: run with #define DEBUG in monitoring.c
// to observe the histogram of one address
#define T_HIT_SAME (157)
#define T_HIT_REMOTE (160)

#define cpuid(ina, inc, a, b, c, d)                                            \
    asm("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(ina), "c"(inc));

int is_intel() {
    // Output registers
    unsigned long eax;

    // Input EAX = 0x0 for vendor identification string
    int eax_in = 0;
    int ecx_in = 0;
    int name[4] = {0, 0, 0, 0};

    cpuid(eax_in, ecx_in, eax, name[0], name[2], name[1]);

    if (strcmp((char *)name, "GenuineIntel") != 0) {
        return 0;
    }

    return 1;
}

int get_cpu_architecture() {
    unsigned int model;
    int name[4] = {0, 0, 0, 0};
    __cpuid(0, model, name[0], name[2], name[1]);
    if (strcmp((char *)name, "GenuineIntel") != 0)
        return -1;
    return model;
}

int get_cpu_model() { // TODO
    // Output registers
    unsigned long eax, ebx, ecx, edx;

    // Input EAX = 0x1 for Type, Family, Model, and Stepping ID
    int eax_in = 1;
    int ecx_in = 0;

    cpuid(eax_in, ecx_in, eax, ebx, ecx, edx);

    int extended_model = (eax & ~(~0 << 4) << 16) >> 16; // EAX, bits 19-16
    int model = (eax & ~(~0 << 4) << 4) >> 4;            // EAX, bits 7-4
    int cpu_model = (extended_model << 4) + model;

    return cpu_model;
}

int partition(int a[], int l, int r) {
    int pivot, i, j, t;
    pivot = a[l];
    i = l;
    j = r + 1;

    while (1) {
        do
            ++i;
        while (a[i] <= pivot && i <= r);
        do
            --j;
        while (a[j] > pivot);
        if (i >= j)
            break;
        t = a[i];
        a[i] = a[j];
        a[j] = t;
    }

    t = a[l];
    a[l] = a[j];
    a[j] = t;
    return j;
}

void quicksort(int a[], int l, int r) {
    int j;
    if (l < r) {
        // divide and conquer
        j = partition(a, l, r);
        quicksort(a, l, j - 1);
        quicksort(a, j + 1, r);
    }
}

void print_cpu() {
    int cpuid_out;

    __asm__("cpuid;" : "=b"(cpuid_out) : "a"(1) :);

    printf("Processor %d\n", cpuid_out >> 24);
}

int is_powerof_two(int x) {
    return ((x != 0) && !(x & (x - 1)));
}

void print_bin(uint64_t val) {
    int i;
    for (i = 63; i >= 0; i--) {
        if ((val >> i) & 1) {
            printf("1");
        } else
            printf("0");
    }
}

int kth_bit(int n, int k) {
    return (n >> k) & 1;
}

int comp(int n) {
    if (n)
        return 0;
    else
        return 1;
}

uint64_t rdtsc_nofence() {
    uint64_t a, d;
    asm volatile("rdtsc" : "=a"(a), "=d"(d));
    a = (d << 32) | a;
    return a;
}

uint64_t rdtsc() {
    uint64_t a, d;
    asm volatile("mfence");
    asm volatile("rdtsc" : "=a"(a), "=d"(d));
    a = (d << 32) | a;
    asm volatile("mfence");
    return a;
}

uint64_t rdtsc_begin() {
    uint64_t a, d;
    asm volatile("mfence\n\t"
                 "CPUID\n\t"
                 "RDTSCP\n\t"
                 "mov %%rdx, %0\n\t"
                 "mov %%rax, %1\n\t"
                 "mfence\n\t"
                 : "=r"(d), "=r"(a)
                 :
                 : "%rax", "%rbx", "%rcx", "%rdx");
    a = (d << 32) | a;
    return a;
}

uint64_t rdtsc_end() {
    uint64_t a, d;
    asm volatile("mfence\n\t"
                 "RDTSCP\n\t"
                 "mov %%rdx, %0\n\t"
                 "mov %%rax, %1\n\t"
                 "CPUID\n\t"
                 "mfence\n\t"
                 : "=r"(d), "=r"(a)
                 :
                 : "%rax", "%rbx", "%rcx", "%rdx");
    a = (d << 32) | a;
    return a;
}

void maccess(void *p) {
    asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax");
}

void flush(void *p) {
    asm volatile("clflush 0(%0)\n" : : "c"(p) : "rax");
}

void prefetch(void *p) {
    asm volatile("prefetcht1 %0" : : "m"(p));
}

void longnop() {
    asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n");
}

const int __endian_bit = 1;
#define is_bigendian() ((*(char *)&__endian_bit) == 0)

uintptr_t read_pagemap(char *path_buf, uintptr_t virt_addr) {
    int i, c, status;
    uint64_t file_offset;
    uintptr_t read_val;
    FILE *f;

    f = fopen(path_buf, "rb");
    if (!f) {
        printf("Error! Cannot open %s\n", path_buf);
        return -1;
    }

    // Shifting by virt-addr-offset number of bytes
    // and multiplying by the size of an address (the size of an entry in
    // pagemap file)
    file_offset = virt_addr / getpagesize() * PAGEMAP_ENTRY;
    status = fseek(f, file_offset, SEEK_SET);
    if (status) {
        perror("Failed to do fseek!");
        return -1;
    }
    errno = 0;
    read_val = 0;
    unsigned char c_buf[PAGEMAP_ENTRY];
    for (i = 0; i < PAGEMAP_ENTRY; i++) {
        c = getc(f);
        if (c == EOF) {
            printf("\nReached end of the file\n");
            return 0;
        }
        if (is_bigendian())
            c_buf[i] = c;
        else
            c_buf[PAGEMAP_ENTRY - i - 1] = c;
    }
    for (i = 0; i < PAGEMAP_ENTRY; i++) {
        read_val = (read_val << 8) + c_buf[i];
    }
    if (GET_BIT(read_val, 63)) {
        // printf("PFN: 0x%llx\n",(unsigned long long) GET_PFN(read_val));
    } else {
        // printf("Page not present\n");
        return 0;
    }
    if (GET_BIT(read_val, 62)) {
        printf("Page swapped\n");
        return 0;
    }
    fclose(f);

    uintptr_t phys_addr;
    phys_addr = GET_PFN(read_val) << 12 | (virt_addr & 0xFFF);

    return phys_addr;
}

int get_cache_slice(uint64_t phys_addr, int nb_cores) {
    static const int h0[] = {6,  10, 12, 14, 16, 17, 18, 20, 22, 24,
                             25, 26, 27, 28, 30, 32, 33, 35, 36};
    static const int h1[] = {7,  11, 13, 15, 17, 19, 20, 21, 22, 23,
                             24, 26, 28, 29, 31, 33, 34, 35, 37};

    int count = sizeof(h0) / sizeof(h0[0]);
    int hash = 0;
    int i;
    for (i = 0; i < count; i++) {
        hash ^= (phys_addr >> h0[i]) & 1;
    }
    if (nb_cores == 2)
        return hash;
    count = sizeof(h1) / sizeof(h1[0]);
    int hash1 = 0;
    for (i = 0; i < count; i++) {
        hash1 ^= (phys_addr >> h1[i]) & 1;
    }
    return hash1 << 1 | hash;
}

size_t flush_hit(char *addr) {
    size_t time = rdtsc();
    flush(addr);
    size_t delta = rdtsc() - time;
    maccess(addr);
    maccess(addr);
    return delta;
}

int same_slice(size_t *hit_histogram) {
    int i;
    int count = 0;
    for (i = 0; i < T_HIT_REMOTE; i++) {
        count += hit_histogram[i];
    }

    if (count > 50)
        return 1;
    return 0;
}

unsigned long threads_per_core() {
    // Output registers
    unsigned long eax, ebx, ecx, edx;

    // Input EAX = 0xB for 2xAPIC information leaf, ECX for sub-leaves
    int eax_in = 11;
    int ecx_in = 0;

    cpuid(eax_in, ecx_in, eax, ebx, ecx, edx);

    return ebx;
}

unsigned long threads_per_package() {
    // Output registers
    unsigned long eax, ebx, ecx, edx;

    // Input EAX = 0xB for 2xAPIC information leaf, ECX for sub-leaves
    int eax_in = 11;
    int ecx_in = 1;

    cpuid(eax_in, ecx_in, eax, ebx, ecx, edx);
    return ebx;
}

unsigned long cores_per_package() {
    return threads_per_package() / threads_per_core();
}

unsigned long apicid2coreid(unsigned long apicid, int *map_apicid,
                            int *map_coreid) {
    int i;
    int core = -1;
    for (i = 0; i < threads_per_package(); i++) {
        if (map_apicid[i] == apicid) {
            core = map_coreid[i];
        }
    }
    return core;
}

/*
 * Get the mapping of each "processor" id to core id
 */
int *mapping_coreid() {
    int *map_coreid;
    map_coreid = (int *)calloc(64, sizeof(int));

    FILE *fp;
    int n = 0;
    int fail = -1;

    // The command on popen output the core id in the order of
    // each "processor" (ie processor 0, then 1, etc)
    fp = popen(
        "awk -F ': ' '/core id/ {print $2}' /proc/cpuinfo | paste -sd ' '",
        "r");

    if (fp == NULL) {
        printf("Failed to run command\n");
        exit(1);
    }

    while (!feof(fp)) {
        fscanf(fp, "%d %n", &map_coreid[n], &fail);
        if (fail == -1) {
            printf("Failed to parse result of command\n");
            exit(1);
        }
        n++;
    }

    pclose(fp);

    return map_coreid;
}

/*
 * Get the mapping of each "processor" id to apic id (warning: contains gaps)
 */
int *mapping_apicid() {
    int *map_apicid;
    map_apicid = (int *)calloc(64, sizeof(int));

    FILE *fp;
    int n = 0;
    int fail = -1;

    // The command on popen output the apic id in the order of
    // each "processor" (ie processor 0, then 1, etc)
    fp = popen("awk -F ': ' '/initial apicid/ {print $2}' /proc/cpuinfo | "
               "paste -sd ' '",
               "r");

    if (fp == NULL) {
        printf("Failed to run command\n");
        exit(1);
    }

    while (!feof(fp)) {
        fscanf(fp, "%d %n", &map_apicid[n], &fail);
        if (fail == -1) {
            printf("Failed to parse result of command\n");
            exit(1);
        }
        n++;
    }

    pclose(fp);

    return map_apicid;
}

unsigned long current_apic(void) {
    // Output registers
    unsigned long eax, ebx, ecx, edx;

    // Input EAX = 0xB for 2xAPIC information leaf, ECX for sub-leaves
    int eax_in = 11;
    int ecx_in = 1;

    cpuid(eax_in, ecx_in, eax, ebx, ecx, edx);

    return edx;
}

unsigned long current_core(void) {
    int *map_apicid = mapping_apicid();
    int *map_coreid = mapping_coreid();

    return apicid2coreid(current_apic(), map_apicid, map_coreid);
}
