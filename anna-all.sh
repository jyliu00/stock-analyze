#!/bin/bash

start_time=`date +%H:%M:%S`

for g in usa ibd biotech 3x china canada
do
    echo "group=$g"
    anna -group=$g $1
    echo ""
done

end_time=`date +%H:%M:%S`

echo "time: $start_time ~ $end_time"
