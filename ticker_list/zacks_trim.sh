#!/bin/bash

echo "%sector=$2" > zacks_$2.txt
cut -d$'\t' -f3,4,7 $1 | sed 'n; d' >> zacks_$2.txt
sed -i 's/[[:blank:]]\{1,\}$//' zacks_$2.txt
sort -k 3 zacks_$2.txt > 1
mv 1 zacks_$2.txt
rm -f $1

#cut -d$'\t' -f3 $1 > .trim.zacks
#sed 'n; d' .trim.zacks > $1
#rm -f .trim.zacks
