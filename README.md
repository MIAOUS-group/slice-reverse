# slice-reverse


Programs scan and reverse
-------------------------

The "scan" program allocates some memory and outputs the count of access for each slice, for each address (with a 64B stride). It thus deduces the slice mapped to each physical address. Run this first to be sure everything works as expected.

The "reverse" program allocates memory in huge pages (2MB pages for Core machines, 1GB for Xeon machines). This is to allocate as much contiguous memory as possible and facilitate the reverse. What would also be possible is to write a driver to directly allocate contiguous physical memory.
Allocating 1GB pages is usually possible with Xeon machines, but only by reserving them at boot time. If the machine has a lot of memory, it will be faster to recover the higher bits in the function than if the huge pages are 2MB. However, if it is not possible to modify the boot parameters for your machine, it is possible to modify the code and use 2MB pages (execution will be slower).



Parameters for the scan programs
----------------------------

-h: help
-c: number of cores
-a: microarchitecture:
    - Xeon Sandy Bridge: "sdb"
    - Xeon Ivy Bridge: "ivb"
    - Xeon Haswell: "hsw"
    - Core Sandy Bridge, Ivy Bridge, and Haswell: "core"



Running the "scan" program
--------------------------

1. Charge the msr module (need to be root)
# modprobe msr

2. Run the scan program (need to be root to access the MSRs), eg for my Sandy Bridge laptop
# ./scan -c 2 -a core

Parameters for the reverse programs
----------------------------

-h: help
FIXME

Running the "reverse" program
-----------------------------

For Core machines:
1. Reserve 2MB pages (need to be root): https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt
# echo 1000 > /proc/sys/vm/nr_hugepages

Note modern kernel may use different paths under /proc/sys

2. Verify the pages have been allocated
$ cat /proc/meminfo | grep -i huge

For Xeon machines:
1. Reserve 1GB pages: can only be done at boot time, by passing parameters to the kernel (can be done in Grub)
default_hugepagesz=1G hugepagesz=1G hugepages=N

2. When the machine is booted, verify that the pages have been allocated
$ cat /proc/meminfo | grep -i huge


Both Core and Xeon:
3. Charge the msr module (need to be root)
# modprobe msr

4. Change the number of pages acquired in reverse.c (`NB_PAGES` for Xeon, `NB_PAGES_CORE` for Core) and make

5. Run the reverse program (need to be root to access the MSRs), eg for my Sandy Bridge laptop
# ./reverse -c 2

If not enough huge pages are allocated, a message will be displayed to inform which bits of the function cannot be retrieved. Maybe try to reboot the machine to acquire more huge pages.
