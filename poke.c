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


#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "global_variables.h"
#include "poke.h"
#include "util.h"

int nb_pokes = 100000;

uintptr_t poke(uintptr_t addr) {
    static uint64_t lastVirtualPage = -1;
    static uint64_t lastPhysPage = -1;

    register int i asm("eax");
    register uintptr_t ptr asm("ebx") = addr;
    uintptr_t paddr;

    for (i = 0; i < nb_pokes; i++) {
        clflush((void *)ptr);
    }

    if (addr >> 12 == lastVirtualPage) {
        paddr = lastPhysPage << 12 | (addr & 0xfffULL);
    } else {
        paddr = read_pagemap("/proc/self/pagemap", (uintptr_t)ptr);
        lastVirtualPage = addr >> 12;
        lastPhysPage = paddr >> 12;
    }
    return paddr;
}
