#!/bin/bash

printf "\n\x1b[33mPotential Double Bottom\x1b[0m:\n\n"

anna -group=$1 check-db

printf "\n\x1b[33mPotential Double Bottom Up\x1b[0m:\n\n"

anna -group=$1 check-dbup

printf "\n\x1b[33mPotential Breakout\x1b[0m:\n\n"

anna -group=$1 check-bo

echo ""