#!/bin/bash

start_time=`date +%H:%M:%S`

for g in usa ibd biotech 3x china #canada
do
    anna -group=$g $1
done

end_time=`date +%H:%M:%S`

echo "time: $start_time ~ $end_time"
