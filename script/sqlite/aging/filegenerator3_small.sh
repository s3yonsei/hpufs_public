#! /bin/bash

output_directory="/mnt/files"

# Test config
#file_count=6250

min_size=16384	# 16KB
max_size=102400	# 100KB

mkdir -p "$output_directory"
mkdir -p "$output_directory/small3"

#for ((i = 1; i <= file_count; i++));
#for file_num in {1..62500}
for file_num in $(seq 120001 180000)
do
	file_size=$((RANDOM * 1000 % (max_size - min_size + 1) + min_size))
	#file_name="smallfile_$i.dat"
	file_name="smallfile_$file_num.dat"

	#dd if=/dev/urandom of="$output_directory/$file_name" bs=1 count="$file_size"
	dd if=/dev/urandom of="$output_directory/small3/$file_name" bs=1 count="$file_size" status=none

	#echo "Created $file_name with size $file_size bytes"
done

echo "Small files generated in $output_directory"

