#!/bin/bash

##
## Copyright (c) 2016-18, Lawrence Livermore National Security, LLC.
##
## Produced at the Lawrence Livermore National Laboratory.
##
## LLNL-CODE-689114
##
## All rights reserved.
##
## This file is part of RAJA.
##
## For details about use and distribution, please read RAJA/LICENSE.
##

BUILD_SUFFIX=lc_chaos-icpc-17.0.174

rm -rf build_${BUILD_SUFFIX} 2>/dev/null
mkdir build_${BUILD_SUFFIX} && cd build_${BUILD_SUFFIX}
. /usr/local/tools/dotkit/init.sh && use cmake-3.4.1 && use gcc-4.9.3p

cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -C ../host-configs/lc-builds/chaos/icpc_17_0_174.cmake \
  -DENABLE_OPENMP=On \
  -DCMAKE_INSTALL_PREFIX=../install_${BUILD_SUFFIX} \
  "$@" \
  ..
