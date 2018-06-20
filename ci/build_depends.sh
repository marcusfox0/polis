#!/usr/bin/env bash

set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
SRCDIR=$(realpath $DIR/..)

export BUILD_TARGET=${BUILD_TARGET:-linux64}
export BUILD_DIR=${BUILD_DIR:-$SRCDIR}
export PULL_REQUEST=${PULL_REQUEST:-false}
export JOB_NUMBER=${JOB_NUMBER:-1}
export BASE_OUTDIR=${BASE_OUTDIR:-$BUILD_DIR/out}
export CCACHE_DIR=$HOME/.ccache-ci

# Precreate ccache dir to avoid root volume ownership
mkdir -p $CCACHE_DIR

docker run -t --rm \
  -w $SRCDIR \
  -v $SRCDIR:$SRCDIR \
  -v $CCACHE_DIR:$CCACHE_DIR \
  -e BASE_OUTDIR="$BASE_OUTDIR" \
  -e PULL_REQUEST="$PULL_REQUEST" \
  -e JOB_NUMBER="$JOB_NUMBER" \
  -e BUILD_DIR="$BUILD_DIR" \
  -e BUILD_TARGET="$BUILD_TARGET" \
  -e CCACHE_DIR=$CCACHE_DIR \
  polis-builder-$BUILD_TARGET ./ci/build_depends_in_builder.sh
