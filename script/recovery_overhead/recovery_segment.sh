echo segment_1024
sudo fio recovery_segment_1024 | grep -E 'WRITE:'
sleep 2m

echo segment_2048
sudo fio recovery_segment_2048 | grep -E 'WRITE:'
sleep 2m

echo segment_4096
sudo fio recovery_segment_4096 | grep -E 'WRITE:'
sleep 2m

echo segment_8192
sudo fio recovery_segment_8192 | grep -E 'WRITE:'
sleep 2m

echo segment_16384
sudo fio recovery_segment_16384 | grep -E 'WRITE:'
sleep 2m

echo segment_32768
sudo fio recovery_segment_32768 | grep -E 'WRITE:'
sleep 2m

echo segment_done