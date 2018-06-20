#!/usr/bin/env bash

set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
SRCDIR=$(realpath $DIR/..)

export BUILD_TARGET=${BUILD_TARGET:-linux64}
export BUILD_DIR=${BUILD_DIR:-$SRCDIR}

docker run -t --rm \
  -w $SRCDIR \
  -v $SRCDIR:$SRCDIR \
  -e BUILD_DIR="$BUILD_DIR" \
  -e BUILD_TARGET="$BUILD_TARGET" \
  -e TRAVIS_BUILD_ID="$TRAVIS_BUILD_ID" \
  polis-builder-$BUILD_TARGET ./ci/test_integrationtests_in_builder.sh
