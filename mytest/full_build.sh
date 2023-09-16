#!/bin/bash

cd ..

make clean

# no -no-pie flags may have such error
# /usr/bin/ld: test/builtin.o: relocation R_X86_64_32S against `.text' can not be used when making a PIE object; recompile with -fPIE
LDFLAGS=-no-pie make fulltest
