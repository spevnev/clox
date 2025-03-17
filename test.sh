#!/bin/bash

BIN="./build/clox"

RESET="\e[0m"
RED="\e[1;31m"
GREEN="\e[1;32m"
BLUE="\e[1;34m"

make TEST=1 -B
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
echo ""

passed=0
failed=0
for file in $(find tests -type f); do
    expected=$(grep '// ' $file | grep -v '///' | sed -re 's/.*\/\/ (.+)/\1/')
    output=$($BIN $file 2>&1)
    name=$(echo $file | cut -d/ -f2-)

    if [ "$output" = "$expected" ]; then
        echo -e $GREEN"[PASSED] $name"$RESET
        ((passed++))
    else
        echo -e $RED"[FAILED] $name"$RESET
        ((failed++))

        echo -e $BLUE"\tExpected:"$RESET
        echo "$expected" | sed -e 's/^/\t/'
        echo -e $BLUE"\tFound:"$RESET
        echo "$output" | sed -e 's/^/\t/'
    fi
done

echo ""
echo "Total:"
echo -e "\tPassed: $passed"
echo -e "\tFailed: $failed"

if [[ $failed > 0 ]]; then
    exit 1
fi
