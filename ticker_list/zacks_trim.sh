#!/bin/bash

cut -d$'\t' -f3 $1 > .trim.zacks
sed 'n; d' .trim.zacks > $1
rm -f .trim.zacks
