#!/bin/bash
############################################################
## Config
############################################################
fsrc="main.c"
fasm="main.asm"
fobj="main.obj"
fexe="main.exe"

############################################################
## Functions
############################################################

function compile_8cc() {
    cd .. || exit
    make clean
    make
    cp ./8cc ./mytest
}

function test_8cc() {
    cd .. || exit
    make clean
    LDFLAGS=-no-pie make test
    # no -no-pie flags may have such error
    # /usr/bin/ld: test/builtin.o: relocation R_X86_64_32S against `.text' can not be used when making a PIE object; recompile with -fPIE
    make clean
    LDFLAGS=-no-pie make fulltest
}

function gen_asm() {
    rm -f "${fasm}"
    ./8cc -S -o "${fasm}" "${fsrc}"
}

function gen_exe() {
    # 1. 调用 8cc 进行汇编，并且 8cc 使用 fork 在子进程中调用 as 进行汇编
    # 2. 调用 gcc 进行链接
    rm -f "${fobj}" "${fexe}"
    ./8cc -c "${fsrc}" -o "${fobj}"
    gcc "${fobj}" -o "${fexe}"
    ./"${fexe}"
}

function gen_clear() {
    rm -f "${fasm}" "${fobj}" "${fexe}"
}

function help_info() {
    echo "Script Args:"
    echo "Compile    编译 8cc 并复制到当前目录"
    echo "Test       编译并测试 8cc"
    echo "GenASM     生成 main.c 的汇编文件"
    echo "GenEXE     生成 main.c 的可执行文件"
    echo "GenClear   清理编译的产物文件"
}

############################################################
## Start
############################################################

PS3='Choose what you want to do: '
choices=("Compile" "Test" "GenASM" "GenEXE" "GenClear" "Help")

select fav in "${choices[@]}"; do
    case "${fav}" in
        "Compile") # 编译 8cc 并复制到当前目录
            compile_8cc
            exit
        ;;
        "Test") # 测试 8cc
            test_8cc
            exit
        ;;

        "GenASM") # 生成 main.c 的汇编文件
            gen_asm
            exit
        ;;

        "GenEXE") # 生成 main.c 的可执行文件
            gen_exe
            exit
            ;;
        "GenClear") # 清理编译产物
            gen_clear
            exit
            ;;
        "Help") # 帮助信息
            help_info
            exit
    esac
done
