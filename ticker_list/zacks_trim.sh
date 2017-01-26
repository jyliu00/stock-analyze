#!/bin/bash

#cut -d$'\t' -f3,4,5,8,10,11,12 $1 | sed 'n; d' > $2
echo "%sector=rank_$2" > zacks_$2.txt
cut -d$'\t' -f3,4,5,6,7,11 $1 | sed 'n; d' >> zacks_$2.txt
rm -f $1

#cut -d$'\t' -f3 $1 > .trim.zacks
#sed 'n; d' .trim.zacks > $1
#rm -f .trim.zacks
