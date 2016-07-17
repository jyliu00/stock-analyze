#!/bin/bash

for g in usa ibd biotech china canada
do
    anna -group=$g $1
done
