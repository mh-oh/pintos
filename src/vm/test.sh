#!/bin/bash

cd build

CMD="pintos -v -k -T 60 --qemu  --filesys-size=2 -p tests/vm/page-parallel -a page-parallel -p tests/vm/child-linear -a child-linear --swap-size=4 -- -q  -f run page-parallel"

for ((i = 9; i <= 50; i++))
do
    eval $CMD "| tee ../outputs/out"$i
done

cd ../