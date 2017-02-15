#!/bin/bash

echo "%sector=rank1_goodvalue" > zacks_rank1_goodvalue.txt
cut -d$'\t' -f3,7,10 $1 | sed 'n; d' >> zacks_rank1_goodvalue.txt
rm -f $1
