#!/bin/bash

./tools/fileops.sh init > /dev/null

DIRECTOARE="bin src include data logs reports tmp tests doc tools"

for dir in $DIRECTOARE
do
	if [ ! -d "$dir" ]
	then
		echo "Eroare, Directoriul '$dir' lipseste"
		exit 1
	fi
done
exit 0


