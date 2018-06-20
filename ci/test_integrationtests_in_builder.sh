#!/usr/bin/env bash

# This script is executed inside the builder image

set -e

source ./ci/matrix.sh

export LD_LIBRARY_PATH=$BUILD_DIR/depends/$HOST/lib

cd build-ci/poliscore-$BUILD_TARGET

./qa/pull-tester/rpc-tests.py --coverage
