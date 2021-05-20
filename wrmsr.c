/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2000 Transmeta Corporation - All Rights Reserved
 *   Copyright 2004-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 *   Boston MA 02110-1301, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * wrmsr.c
 *
 * Utility to write to an MSR.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "wrmsr.h"

const char *program;

/* filter out ".", "..", "microcode" in /dev/cpu */
int dir_filter(const struct dirent *dirp) {
    if (isdigit(dirp->d_name[0]))
        return 1;
    else
        return 0;
}

void wrmsr_on_all_cpus(uint32_t reg, int valcnt, uint64_t *regvals) {
    struct dirent **namelist;
    int dir_entries;

    dir_entries = scandir("/dev/cpu", &namelist, dir_filter, 0);
    while (dir_entries--) {
        wrmsr_on_cpu(reg, atoi(namelist[dir_entries]->d_name), valcnt, regvals);
        free(namelist[dir_entries]);
    }
    free(namelist);
}

void wrmsr_on_cpu(uint32_t reg, int cpu, int valcnt, uint64_t *regvals) {
    uint64_t data;
    int fd;
    char msr_file_name[64];

    sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
    fd = open(msr_file_name, O_WRONLY);
    if (fd < 0) {
        if (errno == ENXIO) {
            fprintf(stderr, "wrmsr: No CPU %d\n", cpu);
            exit(2);
        } else if (errno == EIO) {
            fprintf(stderr, "wrmsr: CPU %d doesn't support MSRs\n", cpu);
            exit(3);
        } else {
            perror("wrmsr: open");
            exit(127);
        }
    }

    while (valcnt--) {
        // data = strtoull(*regvals++, NULL, 0);
        data = *regvals++;
        if (pwrite(fd, &data, sizeof data, reg) != sizeof data) {
            if (errno == EIO) {
                fprintf(stderr,
                        "wrmsr: CPU %d cannot set MSR "
                        "0x%08" PRIx32 " to 0x%016" PRIx64 "\n",
                        cpu, reg, data);
                exit(4);
            } else {
                perror("wrmsr: pwrite");
                exit(127);
            }
        }
    }

    close(fd);

    return;
}

void wrmsr_on_cpu_0(uint32_t reg, int valcnt, uint64_t *regvals) {
    uint64_t data;
    // char msr_file_name[64];
    char *msr_file_name = "/dev/cpu/0/msr";
    int cpu = 0;

    // sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
    static int fd = -1;

    if (fd < 0) {
        fd = open(msr_file_name, O_WRONLY);
        if (fd < 0) {
            if (errno == ENXIO) {
                fprintf(stderr, "wrmsr: No CPU %d\n", cpu);
                exit(2);
            } else if (errno == EIO) {
                fprintf(stderr, "wrmsr: CPU %d doesn't support MSRs\n", cpu);
                exit(3);
            } else {
                perror("wrmsr: open");
                exit(127);
            }
        }
    }

    while (valcnt--) {
        // data = strtoull(*regvals++, NULL, 0);
        data = *regvals++;
        // printf("writing msr 0x%08" PRIx32 " to 0x%016" PRIx64 "\n", reg,
        // data);
        if (pwrite(fd, &data, sizeof data, reg) != sizeof data) {
            if (errno == EIO) {
                fprintf(stderr,
                        "wrmsr: CPU %d cannot set MSR "
                        "0x%08" PRIx32 " to 0x%016" PRIx64 "\n",
                        cpu, reg, data);
                exit(4);
            } else {
                perror("wrmsr: pwrite");
                exit(127);
            }
        }
    }

    // close(fd);

    return;
}
