//
// Created by gdidier on 5/19/21.
//

#ifndef SLICE_REVERSE_ARCH_H
#define SLICE_REVERSE_ARCH_H
/*
 * Declare types
 */

typedef enum {
    CPU_UNKNOWN,
    INTEL_CORE,
    INTEL_XEON,
    MAX_CLASS,
} class_t;

typedef enum {
    UARCH_UNKNOWN,
    SANDY_BRIDGE,
    IVY_BRIDGE,
    HASWELL,
    BROADWELL,
    SKYLAKE,
    KABY_LAKE,
    SKYLAKE_SP,
    // COFFEE_LAKE, -> identical in practice to KBL
    MAX_ARCH,
} uarch_t;

extern const char *const classes_names[MAX_CLASS];
extern const char *const uarch_names[MAX_ARCH];

/*
 * Declare global variables
 */
// FIXME most of these should probably not be globals
extern uarch_t archi;
extern class_t class; // xeon or core
extern int nb_cores;
extern int max_slices;

// FIXME This should probably be turned into a mix of structs and unions
// Or Re-written in rust.

// Xeon and Core processors have rather different performance counters.

// Xeons MSRs and values
extern unsigned long long msr_pmon_ctr0[24];
extern unsigned long long msr_pmon_box_filter[24];
extern unsigned long long msr_pmon_ctl0[24];
extern unsigned long long msr_pmon_box_ctl[24];
extern unsigned long long val_box_freeze;
extern unsigned long long val_box_reset;
extern unsigned long long val_enable_counting;
extern unsigned long long val_select_event;
extern unsigned long long val_filter;
extern unsigned long long val_box_unfreeze;

// Core MSRs and values
extern unsigned long long msr_unc_perf_global_ctr;
extern unsigned long long msr_unc_cbo_perfevtsel0[8];
extern unsigned long long msr_unc_cbo_per_ctr0[8];
extern unsigned long long val_enable_ctrs;
extern unsigned long long val_disable_ctrs;
extern unsigned long long val_select_evt_core;
extern unsigned long long val_reset_ctrs;

int determine_class_uarch(int cpu_model);
int setup_perf_counters(class_t class, uarch_t archi, int nb_cores);
#endif // SLICE_REVERSE_ARCH_H
