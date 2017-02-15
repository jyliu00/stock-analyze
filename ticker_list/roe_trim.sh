#!/bin/bash

echo "%sector=$2_roe" > zacks_$2_roe.txt
cut -d$'\t' -f3,8,9 $1 | sed 'n; d' >> zacks_$2_roe.txt
sort -k 3 zacks_$2_roe.txt > 1
mv 1 zacks_$2_roe.txt
rm -f $1
