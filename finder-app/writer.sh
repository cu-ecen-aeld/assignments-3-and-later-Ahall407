#!/bin/sh
# Write a path to file with a string
# Author: Alex Hall

if [ $# -ne 2 ]
then
    echo "ERROR: Invalid number of arguments."
    echo "Total number of arguments should be 2."
    echo "The order of arguments should be:"
    echo " 1) Full path to a file (including filename)"
    echo " 2) Text string to be written to the file"
    exit 1
fi

writefile=$1
writestr=$2

pathname=$(dirname "$writefile")

mkdir -p "${pathname}"
echo $writestr > $writefile

if [ -e $writefile ]
then
	echo "Successfully wrote ${writestr} to ${writefile}"
else
	echo "ERROR: Failed to write to file ${writefile}"
	exit 1
fi