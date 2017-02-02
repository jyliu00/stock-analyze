#!/bin/bash

cut -d$'\t' -f3,11 $1 | sed 'n; d' > zacks_rank1_goodvalue.txt
rm -f $1
