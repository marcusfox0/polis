#!/usr/bin/env bash

# This script is meant to be sourced into the actual build script. It contains the build matrix and will set all
# necessary environment variables for the request build target

export BUILD_TARGET=${BUILD_TARGET:-linux64}

export GOAL="install"
export MAKEJOBS=-j5
export SDK_URL=${SDK_URL:-https://bitcoincore.org/depends-sources/sdks}
export PYTHON_DEBUG=1

if [ "$BUILD_TARGET" = "arm-linux" ]; then
  export HOST=arm-linux-gnueabihf
  export PACKAGES="g++-arm-linux-gnueabihf"
  export DEP_OPTS="NO_QT=1"
  export CHECK_DOC=1
  export BITCOIN_CONFIG="--enable-glibc-back-compat --enable-reduce-exports"
elif [ "$BUILD_TARGET" = "win32" ]; then
  export HOST=i686-w64-mingw32
  export DPKG_ADD_ARCH="i386"
  export DEP_OPTS="NO_QT=1"
  export PPA="ppa:bitcoin/bitcoin"
  export PACKAGES="python3 nsis g++-mingw-w64-i686 wine-stable wine32 bc"
  export BITCOIN_CONFIG="--enable-gui --enable-reduce-exports"
  export MAKEJOBS="-j4"
  export DIRECT_WINE_EXEC_TESTS=true
elif [ "$BUILD_TARGET" = "win64" ]; then
  export HOST=x86_64-w64-mingw32
  export DPKG_ADD_ARCH="i386"
  export DEP_OPTS="NO_QT=1"
  export PACKAGES="python3 nsis g++-mingw-w64-x86-64 wine-stable wine64 bc"
  export BITCOIN_CONFIG="--enable-gui --enable-reduce-exports"
  export MAKEJOBS="-j4"
  export DIRECT_WINE_EXEC_TESTS=true
elif [ "$BUILD_TARGET" = "linux32" ]; then
  export HOST=i686-pc-linux-gnu
  export PACKAGES="g++-multilib bc python3-zmq"
  export DEP_OPTS="NO_QT=1"
  export BITCOIN_CONFIG="--enable-zmq --enable-glibc-back-compat --enable-reduce-exports LDFLAGS=-static-libstdc++"
  export USE_SHELL="/bin/dash"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "linux64" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export PACKAGES="bc python3-zmq"
  export DEP_OPTS="NO_QT=1 NO_UPNP=1 DEBUG=1"
  export BITCOIN_CONFIG="--enable-zmq --enable-glibc-back-compat --enable-reduce-exports"
  export CPPFLAGS="-DDEBUG_LOCKORDER -DENABLE_DASH_DEBUG"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "linux64_nowallet" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export PACKAGES="python3"
  export DEP_OPTS="NO_WALLET=1"
  export BITCOIN_CONFIG="--enable-glibc-back-compat --enable-reduce-exports"
elif [ "$BUILD_TARGET" = "linux64_release" ]; then
  export HOST=x86_64-unknown-linux-gnu
  export PACKAGES="bc python3-zmq"
  export DEP_OPTS="NO_QT=1 NO_UPNP=1"
  export GOAL="install"
  export BITCOIN_CONFIG="--enable-zmq --enable-glibc-back-compat --enable-reduce-exports"
  export PYZMQ=true
elif [ "$BUILD_TARGET" = "mac" ]; then
  export HOST=x86_64-apple-darwin11
  export PACKAGES="cmake imagemagick libcap-dev librsvg2-bin libz-dev libbz2-dev libtiff-tools"
  export BITCOIN_CONFIG="--enable-gui --enable-reduce-exports"
  export OSX_SDK=10.11
  export GOAL="deploy"
fi
