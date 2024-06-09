echo 1GB
sudo fio test_rw_1g > /dev/null
sudo fio test_rw_1g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

echo 2GB
sudo fio test_rw_2g > /dev/null
sudo fio test_rw_2g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

echo 4GB
sudo fio test_rw_4g > /dev/null
sudo fio test_rw_4g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

echo 8GB
sudo fio test_rw_8g > /dev/null
sudo fio test_rw_8g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

echo 16GB
sudo fio test_rw_16g > /dev/null
sudo fio test_rw_16g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

echo 32GB
sudo fio test_rw_32g > /dev/null
sudo fio test_rw_32g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

echo 64GB
sudo fio test_rw_64g > /dev/null
sudo fio test_rw_64g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

echo 128GB
sudo fio test_rw_120g > /dev/null > /dev/null
sudo fio test_rw_120g | grep -E 'WRITE:'
sudo rm /mnt/rw.0.0

