#!/bin/bash

printf "\n\x1b[33mPotential Double Bottom Up\x1b[0m:\n\n"

anna -group=$1 check-dbup

printf "\n\x1b[33mUp cross SMA20\x1b[0m:\n\n"

anna -group=$1 check-20dup

printf "\n\x1b[33mUp cross SMA50\x1b[0m:\n\n"

anna -group=$1 check-50dup

echo ""
