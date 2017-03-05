#!/bin/bash

printf "\n\x1b[33mTrend Breakout\x1b[0m:\n\n"

anna -group=$1 check-trend-bo

printf "\n\x1b[33mStrong Break Out\x1b[0m:\n\n"

anna -group=$1 check-strong-body-bo

printf "\n\x1b[33mSMA20d BreakOut\x1b[0m:\n\n"

anna -group=$1 check-20d-bo

echo ""
