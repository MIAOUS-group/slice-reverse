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


#include <stdio.h>
#include <string.h>

#include "arch.h"
#include "global_variables.h"

uarch_t archi = UARCH_UNKNOWN;
class_t class = CPU_UNKNOWN; // xeon or core

const char *const classes_names[MAX_CLASS] = {"Unknown Class", "core", "xeon"};
const char *const uarch_names[MAX_ARCH] = {
    "Unkown uarch", "Sandy Bridge", "Ivy Bridge", "Haswell",
    "Broadwell",    "Skylake",      "Kaby Lake",  "Skylake SP"};

int nb_cores;
int max_slices;

// Xeons MSRs and values
unsigned long long msr_pmon_ctr0[24] = {0};
unsigned long long msr_pmon_box_filter[24] = {0};
unsigned long long msr_pmon_ctl0[24] = {0};
unsigned long long msr_pmon_box_ctl[24] = {0};
unsigned long long val_box_freeze = -1;
unsigned long long val_box_reset = -1;
unsigned long long val_enable_counting = -1;
unsigned long long val_select_event = -1;
unsigned long long val_filter = -1;
unsigned long long val_box_unfreeze = -1;

// Core MSRs and values
unsigned long long msr_unc_perf_global_ctr = -1;
unsigned long long msr_unc_cbo_perfevtsel0[8] = {0};
unsigned long long msr_unc_cbo_per_ctr0[8] = {0};
unsigned long long val_enable_ctrs = -1;
unsigned long long val_disable_ctrs = -1;
unsigned long long val_select_evt_core = -1;
unsigned long long val_reset_ctrs = -1;

int determine_class_uarch(int cpu_model) {

    // CPU class: Xeon or Core
    switch (cpu_model) {
    case 45:
    case 62:
    case 63:
    case 86:
    case 79:
    case 85:
        class = INTEL_XEON;
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
        class = INTEL_CORE;
        break;
    default:
        class = CPU_UNKNOWN;
        printf("CPU is undefined\n");
        return -1;
    }

    // CPU micro-architecture
    switch (cpu_model) {
    case 45:
    case 42:
        archi = SANDY_BRIDGE; // Sandy Bridge
        break;
    case 62:
    case 58:
        archi = IVY_BRIDGE; // Ivy Bridge
        break;
    case 63:
    case 60:
    case 69:
    case 70:
        archi = HASWELL; // Haswell
        break;
    case 86:
    case 79:
    case 61:
    case 71:
        archi = BROADWELL; // Broadwell
        break;
    case 78:
    case 94:
        archi = SKYLAKE; // Skylake (core)
        break;
    case 85:
        archi = SKYLAKE_SP; // Skyake (xeon) -> not supported yet
        printf(
            "Micro-architecure not supported (Skylake SP)\n"); // Should trigger
                                                               // warning later
        break;
    case 142:
    case 158:
        archi = KABY_LAKE; // Kaby Lake or Coffee Lake
        break;
    default:
        archi = UARCH_UNKNOWN;
        printf("Micro-architecture is undefined\n");
        return -1;
    }

    printf("Micro-architecture: %s %s\n", classes_names[class],
           uarch_names[archi]);
    printf("Number of cores: %d\n", nb_cores);

    return 0;
}

int setup_perf_counters(class_t class, uarch_t archi, int nb_cores) {

    // Xeons
    if (class == INTEL_XEON) {

        if (archi == SANDY_BRIDGE) {
            max_slices = 8;
            unsigned long long *_msr_pmon_ctr0 = (unsigned long long[]){0xd16, 0xd36, 0xd56, 0xd76,
                                                   0xd96, 0xdb6, 0xdd6, 0xdf6};
            memcpy(msr_pmon_ctr0,_msr_pmon_ctr0, max_slices * sizeof (unsigned long long));
            unsigned long long *_msr_pmon_box_filter = (unsigned long long[]){
                0xd14, 0xd34, 0xd54, 0xd74, 0xd94, 0xdb4, 0xdd4, 0xdf4};
            memcpy(msr_pmon_box_filter,_msr_pmon_box_filter, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_ctl0 = (unsigned long long[]){0xd10, 0xd30, 0xd50, 0xd70,
                                                   0xd90, 0xdb0, 0xdd0, 0xdf0};
            memcpy(msr_pmon_ctl0,_msr_pmon_ctl0, max_slices * sizeof (unsigned long long));
            unsigned long long *_msr_pmon_box_ctl = (unsigned long long[]){
                0xd04, 0xd24, 0xd44, 0xd64, 0xd84, 0xda4, 0xdc4, 0xde4};
            memcpy(msr_pmon_box_ctl,_msr_pmon_box_ctl, max_slices * sizeof (unsigned long long));
            val_box_freeze = 0x10100;
            val_box_reset = 0x10103;
            val_enable_counting = 0x400000;
            val_select_event = 0x401134;
            val_filter = 0x7c0000;
            val_box_unfreeze = 0x10000;
        } else if (archi == IVY_BRIDGE) {
            max_slices = 15;
            unsigned long long *_msr_pmon_ctr0 = (unsigned long long[]){
                0xd16, 0xd36, 0xd56, 0xd76, 0xd96, 0xdb6, 0xdd6, 0xdf6,
                0xe16, 0xe36, 0xe56, 0xe76, 0xe96, 0xeb6, 0xed6};
            memcpy(msr_pmon_ctr0,_msr_pmon_ctr0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_box_filter = (unsigned long long[]){
                0xd14, 0xd34, 0xd54, 0xd74, 0xd94, 0xdb4, 0xdd4, 0xdf4,
                0xe14, 0xe34, 0xe54, 0xe74, 0xe94, 0xeb4, 0xed4};
            memcpy(msr_pmon_box_filter,_msr_pmon_box_filter, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_ctl0 = (unsigned long long[]){
                0xd10, 0xd30, 0xd50, 0xd70, 0xd90, 0xdb0, 0xdd0, 0xdf0,
                0xe10, 0xe30, 0xe50, 0xe70, 0xe90, 0xeb0, 0xed0};
            memcpy(msr_pmon_ctl0,_msr_pmon_ctl0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_box_ctl = (unsigned long long[]){
                0xd04, 0xd24, 0xd44, 0xd64, 0xd84, 0xda4, 0xdc4, 0xde4,
                0xe04, 0xe24, 0xe44, 0xe64, 0xe84, 0xea4, 0xec4};
            memcpy(msr_pmon_box_ctl,_msr_pmon_box_ctl, max_slices * sizeof (unsigned long long));

            val_box_freeze = 0x30100;
            val_box_reset = 0x30103;
            val_enable_counting = 0x400000;
            val_select_event = 0x401134;
            val_filter = 0x7e0010;
            val_box_unfreeze = 0x30000;
        } else if (archi == HASWELL) {
            max_slices = 18;
            unsigned long long *_msr_pmon_ctr0 = (unsigned long long[]){
                0xe08, 0xe18, 0xe28, 0xe38, 0xe48, 0xe58, 0xe68, 0xe78, 0xe88,
                0xe98, 0xea8, 0xeb8, 0xec8, 0xed8, 0xee8, 0xef8, 0xf08, 0xf18};
            memcpy(msr_pmon_ctr0,_msr_pmon_ctr0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_box_filter = (unsigned long long[]){
                0xe05, 0xe15, 0xe25, 0xe35, 0xe45, 0xe55, 0xe65, 0xe75, 0xe85,
                0xe95, 0xea5, 0xeb5, 0xec5, 0xed5, 0xee5, 0xef5, 0xf05, 0xf15};
            memcpy(msr_pmon_box_filter,_msr_pmon_box_filter, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_ctl0 = (unsigned long long[]){
                0xe01, 0xe11, 0xe21, 0xe31, 0xe41, 0xe51, 0xe61, 0xe71, 0xe81,
                0xe91, 0xea1, 0xeb1, 0xec1, 0xed1, 0xee1, 0xef1, 0xf01, 0xf11};
            memcpy(msr_pmon_ctl0,_msr_pmon_ctl0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_box_ctl = (unsigned long long[]){
                0xe00, 0xe10, 0xe20, 0xe30, 0xe40, 0xe50, 0xe60, 0xe70, 0xe80,
                0xe90, 0xea0, 0xeb0, 0xec0, 0xed0, 0xee0, 0xef0, 0xf00, 0xf10};
            memcpy(msr_pmon_box_ctl,_msr_pmon_box_ctl, max_slices * sizeof (unsigned long long));

            val_box_freeze = 0x30100;
            val_box_reset = 0x30103;
            val_enable_counting = 0x400000;
            val_select_event = 0x401134;
            val_filter = 0x7e0020;
            val_box_unfreeze = 0x30000;
        } else if (archi == BROADWELL) {
            max_slices = 24;
            unsigned long long *_msr_pmon_ctr0 = (unsigned long long[]){
                0xe08, 0xe18, 0xe28, 0xe38, 0xe48, 0xe58, 0xe68, 0xe78,
                0xe88, 0xe98, 0xea8, 0xeb8, 0xec8, 0xed8, 0xee8, 0xef8,
                0xf08, 0xf18, 0xf28, 0xf38, 0xf48, 0xf58, 0xf68, 0xf78};
            memcpy(msr_pmon_ctr0,_msr_pmon_ctr0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_box_filter = (unsigned long long[]){
                0xe05, 0xe15, 0xe25, 0xe35, 0xe45, 0xe55, 0xe65, 0xe75,
                0xe85, 0xe95, 0xea5, 0xeb5, 0xec5, 0xed5, 0xee5, 0xef5,
                0xf05, 0xf15, 0xf25, 0xf35, 0xf45, 0xf55, 0xf65, 0xf75};
            memcpy(msr_pmon_box_filter,_msr_pmon_box_filter, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_ctl0 = (unsigned long long[]){
                0xe01, 0xe11, 0xe21, 0xe31, 0xe41, 0xe51, 0xe61, 0xe71,
                0xe81, 0xe91, 0xea1, 0xeb1, 0xec1, 0xed1, 0xee1, 0xef1,
                0xf01, 0xf11, 0xf21, 0xf31, 0xf41, 0xf51, 0xf61, 0xf71};
            memcpy(msr_pmon_ctl0,_msr_pmon_ctl0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_pmon_box_ctl = (unsigned long long[]){
                0xe00, 0xe10, 0xe20, 0xe30, 0xe40, 0xe50, 0xe60, 0xe70,
                0xe80, 0xe90, 0xea0, 0xeb0, 0xec0, 0xed0, 0xee0, 0xef0,
                0xf00, 0xf10, 0xf20, 0xf30, 0xf40, 0xf50, 0xf60, 0xf70};
            memcpy(msr_pmon_box_ctl,_msr_pmon_box_ctl, max_slices * sizeof (unsigned long long));

            val_box_freeze = 0x30100;
            val_box_reset = 0x30103;
            val_enable_counting = 0x400000;
            val_select_event = 0x401134;
            val_filter = 0xfe0020;
            val_box_unfreeze = 0x30000;
        }
    }
    // Cores
    else if (class == INTEL_CORE) {

        max_slices = 4;
        if (archi == SKYLAKE || archi == KABY_LAKE) { // >= skylake

            msr_unc_perf_global_ctr = 0xe01;
            val_enable_ctrs = 0x20000000;
            max_slices = 7;
            if (nb_cores ==
                8) { // 8 core client coffee lakes are missing one CBox.
                nb_cores = 7; // we use the 7 known ones and the 8th values can
                              // be deduced
            }
            unsigned long long *_msr_unc_cbo_perfevtsel0 = (unsigned long long[]){
                0x700, 0x710, 0x720, 0x730, 0x740, 0x750, 0x760};
            memcpy(msr_unc_cbo_perfevtsel0,_msr_unc_cbo_perfevtsel0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_unc_cbo_per_ctr0 = (unsigned long long[]){
                0x706, 0x716, 0x726, 0x736, 0x746, 0x756, 0x766};
            memcpy(msr_unc_cbo_per_ctr0,_msr_unc_cbo_per_ctr0, max_slices * sizeof (unsigned long long));

        } else {
            msr_unc_perf_global_ctr = 0x391;
            val_enable_ctrs = 0x2000000f;
            unsigned long long *_msr_unc_cbo_perfevtsel0 =
                (unsigned long long[]){0x700, 0x710, 0x720, 0x730};
            memcpy(msr_unc_cbo_perfevtsel0,_msr_unc_cbo_perfevtsel0, max_slices * sizeof (unsigned long long));

            unsigned long long *_msr_unc_cbo_per_ctr0 =
                (unsigned long long[]){0x706, 0x716, 0x726, 0x736};
            memcpy(msr_unc_cbo_per_ctr0,_msr_unc_cbo_per_ctr0, max_slices * sizeof (unsigned long long));

        }
        val_disable_ctrs = 0x0;
        val_select_evt_core = 0x408f34;
        val_reset_ctrs = 0x0;
    }
    return 0;
}
