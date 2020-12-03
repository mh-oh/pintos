#!/bin/bash

cd build

CMD="pintos -v -k -T 3600 --qemu  --filesys-size=2 -p tests/vm/page-parallel -a page-parallel -p tests/vm/child-linear -a child-linear --swap-size=4 -- -q  -f run page-parallel"

for ((i = 0; i < 100; i++))
do
    eval $CMD "| tee ../outputs/out"$i
    python3 ../clear.py "../outputs/out"$i
    python3 ../memchk.py "../outputs/out"$i
done

cd ../