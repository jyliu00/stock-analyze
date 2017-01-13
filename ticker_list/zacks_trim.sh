#!/bin/bash

#cut -d$'\t' -f3,4,5,8,10,11,12 $1 | sed 'n; d' > $2
cut -d$'\t' -f3,4,5,6,7 $1 | sed 'n; d' > $2
rm -f $1

#cut -d$'\t' -f3 $1 > .trim.zacks
#sed 'n; d' .trim.zacks > $1
#rm -f .trim.zacks
