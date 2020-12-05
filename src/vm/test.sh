#!/bin/bash

rm -rf ./outputs
mkdir ./outputs
cd build

#RUN="pintos -v -k -T 3600 --qemu  --filesys-size=2 -p tests/vm/page-parallel -a page-parallel -p tests/vm/child-linear -a child-linear --swap-size=4 -- -q  -f run page-parallel"
RUN="make check"

for ((i = 0; i < 250; i++))
do
    #FILE="../outputs/page-parallel-"$i
    #STAT="../outputs/stat"

    #eval $RUN > $FILE".debug"
    #grep -v "^#####" $FILE".debug" > $FILE".output"
    #perl -I../.. ../../tests/vm/page-parallel.ck $FILE $FILE".result"
    #python3 ../resource-chk.py $FILE".debug"

    #python3 ../stat.py $FILE $STAT

    FILE="../outputs/make-check-"$i
    STAT="../outputs/stat"

    eval $RUN > $FILE".debug"
    make clean
done

cd ../