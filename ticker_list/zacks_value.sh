#!/bin/bash

cut -d$'\t' -f2 $1 > 2
sort -u 2 > zacks_$2.txt
rm -f 2 $1
