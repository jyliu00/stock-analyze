#!/bin/bash

printf "\n\x1b[33mSMA50d Up\x1b[0m:\n\n"

anna -group=$1 check-50dup $2

printf "\n\x1b[33mStrong Body BreakOut\x1b[0m:\n\n"

anna -group=$1 check-strong-body-bo $2

printf "\n\x1b[33mTrend BreakOut\x1b[0m:\n\n"

anna -group=$1 check-trend-bo $2

printf "\n\x1b[33mSMA20d BreakOut\x1b[0m:\n\n"

anna -group=$1 check-20d-bo $2

printf "\n\x1b[33mSMA10d BreakOut\x1b[0m:\n\n"

anna -group=$1 check-10d-bo $2

echo ""
