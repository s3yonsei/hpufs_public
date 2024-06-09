# High-performance UFS

This repository is for the paper entitled "Fully Harnessing the Performance Potential of DRAM-less Mobile Flash Storage," in the The 38th International Conference on Massive Storage Systems and Technology.

Installation
1. Install FEMU : https://github.com/MoatLab/FEMU

2. Change FEMU files and folders
 - femu/build-femu/run-blackbox.sh
 - femu/hw/femu

3. Install the kernel (5.16.20) (QEMU)

4. Change kernel files and folders (QEMU)
 - drivers/nvme/host
 - include/linux

5. install fio

6. Copy Script File (QEMU)
 - 4_performance
 - application
 - garbage_collection
 - random_write
 - recovery_overhead
 - sqlite

7. Running .sh within each folder allows you to view HP-UFS performance

