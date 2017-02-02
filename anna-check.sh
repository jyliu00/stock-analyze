#!/bin/bash

printf "\n\x1b[33mBreak Out\x1b[0m:\n\n"

anna -group=$1 check-bo

printf "\n\x1b[33mTrend Breakout\x1b[0m:\n\n"

anna -group=$1 check-trend-bo

echo ""
