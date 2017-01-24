#!/bin/bash

printf "\n\x1b[33mTrend Breakout\x1b[0m:\n\n"

anna -group=$1 check-trend-bo

printf "\n\x1b[33mCross SMA20 above\x1b[0m:\n\n"

anna -group=$1 check-20dup

printf "\n\x1b[33mCross SMA50 above\x1b[0m:\n\n"

anna -group=$1 check-50dup

echo ""
