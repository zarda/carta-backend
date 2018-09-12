#!/bin/bash

cd ../../ThirdParty

export INSTALLHOME=`pwd`

echo $INSTALLHOME

DIR="zfp"
if [ -d $DIR ]; then
    echo $DIR Exists!
    rm -rf $DIR
fi

DIR2="zfp-src"
if [ -d $DIR2 ]; then
    echo $DIR2 Exists!
    rm -rf $DIR2
fi

git clone https://github.com/LLNL/zfp.git

mv zfp zfp-src

mkdir zfp

cd zfp-src

mkdir build

cd build

cmake -DCMAKE_INSTALL_PREFIX:PATH=$INSTALLHOME/zfp ..

make

make install
