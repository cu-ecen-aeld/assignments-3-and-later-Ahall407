#!/bin/sh
# Search directory for string
# Author: Alex Hall

if [ $# -ne 2 ]
then
    echo "ERROR: Invalid number of arguments."
    echo "Total number of arguments should be 2."
    echo "The order of arguments should be:"
    echo " 1) File directory path"
    echo " 2) String to search for"
    exit 1
fi



filesdir=$1
searchstr=$2

num_files=$(find "${filesdir}/" -type f | wc -l)

if [ -d $filesdir ]
then
	num_matching_lines=$(grep -r "$searchstr" "$filesdir" | wc -l)
else
	echo "${filesdir} is not a directory"
	exit 1
fi

if [ $? -eq 0 ]
then
	echo "Success"
	echo "The number of files are ${num_files} and the number of matching lines are ${num_matching_lines}"
	exit 0
else
	echo "Failed: Expected ${searchstr} in ${filesdir} but none found"
	exit 1
fi