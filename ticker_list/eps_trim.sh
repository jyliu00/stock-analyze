#!/bin/bash

echo "%sector=$2" > zacks_$2.txt
cut -d$'\t' -f3,8,9 $1 | sed 'n; d' >> zacks_$2.txt
sed -i 's/[[:blank:]]\{1,\}$//' zacks_$2.txt
sort -k 3 zacks_$2.txt > 1
mv 1 zacks_$2.txt
rm -f $1
