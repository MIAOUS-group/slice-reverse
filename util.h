#ifndef HIDEMINMAX
#define MAX(X,Y) (((X) > (Y)) ? (X) : (Y))
#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))
#endif

#define clflush(p) \
  asm volatile ("clflush (%0)" :: "r"(p));

int is_intel();
int get_cpu_architecture();
int get_cpu_model();
int partition(int a[], int l, int r);
void quicksort(int a[], int l, int r);
void print_cpu();
int is_powerof_two(int x);
void print_bin(uint64_t val);
int kth_bit(int n, int k);
int comp(int n);
uint64_t rdtsc_nofence();
uint64_t rdtsc();
uint64_t rdtsc_begin();
uint64_t rdtsc_end();
void maccess(void* p);
void flush(void* p);
void prefetch(void* p);
void longnop();
uintptr_t read_pagemap(char * path_buf, uintptr_t virt_addr);
int get_cache_slice(uint64_t phys_addr, int nb_cores);
size_t flush_hit(char* addr);
int same_slice(size_t* hit_histogram);
unsigned long threads_per_core();
unsigned long threads_per_package();
unsigned long cores_per_package();
unsigned long apicid2coreid(unsigned long apicid, int* map_apicid, int* map_coreid);
int * mapping_coreid();
int * mapping_apicid();
unsigned long current_apic(void);
unsigned long current_core(void);
