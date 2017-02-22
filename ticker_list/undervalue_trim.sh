#!/bin/bash

echo "%sector=$2_undervalue" > zacks_$2_undervalue.txt
cut -d$'\t' -f3,4,7 $1 | sed 'n; d' >> zacks_$2_undervalue.txt
sed -i 's/[[:blank:]]\{1,\}$//' zacks_$2_undervalue.txt
sort -k 3 zacks_$2_undervalue.txt > 1
mv 1 zacks_$2_undervalue.txt
rm -f $1
