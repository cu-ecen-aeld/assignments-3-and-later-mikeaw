#!/bin/sh
# 
# Author: Michael Wilder

if [ $# -ne 2 ]
then
 echo "ERROR: The script takes 2 arguments\n 1) Full path to a file to be created\n 2) Text string to be added to file"
 exit 1
fi

writefile=$1
writestr=$2

mkdir -p "$(dirname "$writefile")" || {
  echo "ERROR: File could not be created"
  exit 1
}

  echo "$writestr" > "$writefile" || {
  echo "ERROR: File could not be created"
  exit 1
}
