#!/bin/sh

cut -d$'\t' -f3 $1 > 2
sed 'n; d' 2 > zacks_eps.txt
rm -f $1 2
