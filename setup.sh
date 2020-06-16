#!/usr/bin/env bash

echo "Setting up kernel module and huge pages for reverse-engineering..."

if [[ $(id -u) -ne 0 ]]
  then echo "Error: Please run as root"
  exit
fi

# The msr kernel module is necessary to read performance counters
modprobe msr

# Reset number of huge pages
echo 0 | tee /proc/sys/vm/nr_hugepages

# Parameters
mempercent=0.9 # Percentage of free memory used

# Evaluating free memory and how many 2MB we can reserve
memfree=$(grep MemFree /proc/meminfo | awk '{print $2}')
hugepagesfree=$((memfree / 2048))
hugepagesallocate=$(expr $hugepagesfree*$mempercent | bc -l | xargs printf %.0f)

# 2MB pages are used to speed up the reverse-engineering
echo $hugepagesallocate | tee /proc/sys/vm/nr_hugepages
cat /proc/meminfo | grep -i 'HugePages\_Total\|HugePages\_Free'
