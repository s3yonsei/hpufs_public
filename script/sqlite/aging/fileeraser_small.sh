#! /bin/bash

output_directory="/mnt/files"

# Test config
file_count=50000
#file_count=5000

erase_count=0

# Test config

min_num=1
max_num=60000
for count in $(seq 1 12500)
do
	file_num=$(((RANDOM * 1000 + RANDOM) % (max_num - min_num + 1) + min_num))
	file_name="smallfile_$file_num.dat"

	if [ -e "$output_directory/small1/$file_name" ]; then
		#echo "Erase $file_name"
		erase_count=$(($erase_count + 1))
		rm "$output_directory/small1/$file_name"
	fi
done

min_num=60001
max_num=120000
for count in $(seq 1 12500)
do
	file_num=$(((RANDOM * 1000 + RANDOM) % (max_num - min_num + 1) + min_num))
	file_name="smallfile_$file_num.dat"

	if [ -e "$output_directory/small2/$file_name" ]; then
		#echo "Erase $file_name"
		erase_count=$(($erase_count + 1))
		rm "$output_directory/small2/$file_name"
	fi
done

min_num=120001
max_num=180000
for count in $(seq 1 12500)
do
	file_num=$(((RANDOM * 1000 + RANDOM) % (max_num - min_num + 1) + min_num))
	file_name="smallfile_$file_num.dat"

	if [ -e "$output_directory/small3/$file_name" ]; then
		#echo "Erase $file_name"
		erase_count=$(($erase_count + 1))
		rm "$output_directory/small3/$file_name"
	fi
done

min_num=180001
max_num=240000
for count in $(seq 1 12500)
do
	file_num=$(((RANDOM * 1000 + RANDOM) % (max_num - min_num + 1) + min_num))
	file_name="smallfile_$file_num.dat"

	if [ -e "$output_directory/small4/$file_name" ]; then
		#echo "Erase $file_name"
		erase_count=$(($erase_count + 1))
		rm "$output_directory/small4/$file_name"
	fi
done

echo "$erase_count of $file_count small files erased in $output_directory"

