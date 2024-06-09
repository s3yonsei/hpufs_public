(echo p;
 echo n;
 echo p;
 echo 1;
 echo 256;
 echo 27787263;
 echo p;
 echo w) | fdisk /dev/nvme0n1

sleep .5
mkfs.ext4 /dev/nvme0n1p1

sleep .5
mount -o nobarrier -o noatime -o nodiratime -o data=writeback /dev/nvme0n1p1 /mnt
