#!/bin/bash

cd ..

make clean

bear make -j2

cd ./mytest

cp ../8cc .

./8cc -S -o main.asm main.c
