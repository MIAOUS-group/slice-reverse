extern int nb_cores;
extern int max_slices;
extern int nb_pokes;

// Xeons MSRs and values
extern unsigned long long * msr_pmon_ctr0;
extern unsigned long long * msr_pmon_box_filter;
extern unsigned long long * msr_pmon_ctl0;
extern unsigned long long * msr_pmon_box_ctl;
extern unsigned long long val_box_freeze;
extern unsigned long long val_box_reset;
extern unsigned long long val_enable_counting;
extern unsigned long long val_select_event;
extern unsigned long long val_filter;
extern unsigned long long val_box_unfreeze;

// Core MSRs and values
extern unsigned long long msr_unc_perf_global_ctr;
extern unsigned long long * msr_unc_cbo_perfevtsel0;
extern unsigned long long * msr_unc_cbo_per_ctr0;
extern unsigned long long val_enable_ctrs;
extern unsigned long long val_disable_ctrs;
extern unsigned long long val_select_evt_core;
extern unsigned long long val_reset_ctrs;
