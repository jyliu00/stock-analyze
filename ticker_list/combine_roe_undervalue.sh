#!/bin/bash

cat zacks_rank1_roe.txt > 1
cat zacks_rank1_undervalue.txt >> 1
sort -k 3 1 > 2
uniq 2 > zacks_rank1_roe_undervalue.txt
rm -f 1 2
