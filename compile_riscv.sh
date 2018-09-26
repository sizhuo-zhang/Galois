#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 BUILD_DIR"
    exit 1
fi

cur_dir=`pwd`
boost_dir=$cur_dir/../boost_1_62_0/build
build_dir=$1

mkdir -p $build_dir
cd $build_dir

export BOOST_DIR=$boost_dir
cmake -DUSE_ARCH=rv64imafd -DCMAKE_TOOLCHAIN_FILE=$cur_dir/cmake/Toolchain/riscv.cmake $cur_dir
