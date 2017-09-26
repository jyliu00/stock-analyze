#!/bin/sh

cut -d$'\t' -f3 $1 > 2
sed 'n; d' 2 > 3
echo %sector=zacks_$2 > zacks_$2.txt
cat 3 >> zacks_$2.txt

rm -f 3 2 $1
