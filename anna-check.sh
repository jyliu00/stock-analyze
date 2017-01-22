#!/bin/bash

printf "\n\x1b[33mTrend Breakout\x1b[0m:\n\n"

anna -group=$1 check-trend-bo

printf "\n\x1b[33mExtreme Volume at Support\x1b[0m:\n\n"

anna -group=$1 check-volume-spt

echo ""
