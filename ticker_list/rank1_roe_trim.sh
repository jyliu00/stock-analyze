#!/bin/bash

echo "%sector=rank1_roe" > zacks_rank1_roe.txt
cut -d$'\t' -f3,9,10 $1 | sed 'n; d' >> zacks_rank1_roe.txt
rm -f $1
