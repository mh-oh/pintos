#!/bin/bash

cd build

CMD="pintos -v -k -T 120 --qemu  --filesys-size=2 -p tests/vm/page-parallel -a page-parallel -p tests/vm/child-linear -a child-linear --swap-size=4 -- -q  -f run page-parallel"

for ((i = 0; i < 100; i++))
do
    eval $CMD "| tee ../outputs-short-time-slice/out"$i
done

cd ../