#!/bin/bash

cut -d$'\t' -f2 $1 > 2
sort -u 2 > 3
echo %sector=zacks_$2 > zacks_$2.txt
cat 3 >> zacks_$2.txt

rm -f 3 2 $1
