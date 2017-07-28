#!/bin/bash

cut -d$'\t' -f2 $1 > 2
sort -u 2 > zacks_value.txt
rm -f 2 $1
