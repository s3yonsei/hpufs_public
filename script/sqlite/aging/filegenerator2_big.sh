#! /bin/bash

output_directory="/mnt/files"

#Test count
file_count=125 # half or 250
#file_count=13

min_size=104857600	# 100MB
max_size=209715200	# 200MB

mkdir -p "$output_directory"

#for ((i = 1; i <= file_count; i++));
for file_num in $(seq 301 600)
do
	file_size=$((RANDOM *100000 % (max_size - min_size + 1) + min_size))
	write_count=$(($file_size/16))
	file_name="bigfile2_$file_num.dat"

	#dd if=/dev/urandom of="$output_directory/$file_name" bs=1 count="$file_size"
	#dd if=/dev/urandom of="$output_directory/$file_name" bs=1 count="$file_size" status=none
	dd if=/dev/urandom of="$output_directory/$file_name" bs=16 count="$write_count" status=none

	#echo "Created $file_name with size $file_size bytes"
done

echo "Big files generated in $output_directory"

