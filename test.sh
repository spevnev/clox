#!/bin/bash

RESET="\e[0m"
RED="\e[1;31m"
GREEN="\e[1;32m"
BLUE="\e[1;34m"

bin="./build/clox"

make TEST=1 -B
ret=$?
if [ $ret -ne 0 ]; then exit $ret; fi
echo ""

passed=0
failed=0
for file in $(find tests -type f); do
    expected=$(grep '// ' $file | grep -v '///' | sed -re 's/.*\/\/ (.+)/\1/')
    output=$($bin $file 2>&1)
    name=$(echo $file | cut -d/ -f2-)

    if [ "$output" = "$expected" ]; then
        echo -e $GREEN"[PASSED] $name"$RESET
        ((passed += 1))
    else
        echo -e $RED"[FAILED] $name"$RESET
        ((failed += 1))

        echo -e $BLUE"    Expected:"$RESET
        echo "$expected" | sed -e 's/^/    /'
        echo -e $BLUE"    Found:"$RESET
        echo "$output" | sed -e 's/^/    /'
    fi
done

echo ""
echo "Total:"
echo "    Passed: $passed"
echo "    Failed: $failed"

if [ $failed -gt 0 ]; then
    exit 1
fi
