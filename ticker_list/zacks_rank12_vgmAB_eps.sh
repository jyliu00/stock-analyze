#!/bin/sh

cut -d$'\t' -f3 $1 > 2
sed 'n; d' 2 > zacks_rank12_vgmAB_eps.txt
rm -f $1 2
