#!/bin/sh
# 
# Author: Michael Wilder

if [ $# -ne 2 ]
then
 echo "ERROR: The script takes 2 arguments\n 1) Path to a directory\n 2) Text string to be searched"
 exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]
then
 echo "ERROR: 1st argument must be an existing directory on the filesystem"
 exit 1
fi

#below gives number of files
X=$(find "$filesdir" -type f | wc -l)
#below gives number of lines that contain the keyword
Y=$(grep -r -- "${searchstr}" "${filesdir}" | wc -l)

echo "The number of files are $X and the number of matching lines are $Y"
