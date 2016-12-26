#!/bin/bash

cut -d$'\t' -f3,4,5 $1 | sed 'n; d' > zacks_1n2.txt

#cut -d$'\t' -f3 $1 > .trim.zacks
#sed 'n; d' .trim.zacks > $1
#rm -f .trim.zacks
