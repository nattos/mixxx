#!/bin/bash
# This script works with Debian, Ubuntu, and derivatives.
# shellcheck disable=SC1091
set -o pipefail

sudo apt-get update

# If jackd2 is installed as per dpkg database, install libjack-jackd2-dev.
# This avoids a package deadlock, resulting in jackd2 being removed, and jackd1 being installed,
# to satisfy portaudio19-dev's need for a jackd dev package. In short, portaudio19-dev needs a
# jackd dev library, so let's give it one..
if [ "$(dpkg-query -W -f='${Status}' jackd2 2>/dev/null | grep -c "ok installed")" -eq 1 ]; then
    sudo apt-get install libjack-jackd2-dev;
fi

sudo apt-get install -y --no-install-recommends -- \
ccache \
cmake \
clazy \
clang-tidy \
debhelper \
devscripts \
docbook-to-man \
dput \
fonts-open-sans \
fonts-ubuntu \
g++ \
lcov \
libavformat-dev \
libchromaprint-dev \
libdistro-info-perl \
libebur128-dev \
libfaad-dev \
libfftw3-dev \
libflac-dev \
libhidapi-dev \
libid3tag0-dev \
liblilv-dev \
libmad0-dev \
libmodplug-dev \
libmp3lame-dev \
libopus-dev \
libopusfile-dev \
libportmidi-dev \
libprotobuf-dev \
libqt5opengl5-dev \
libqt5sql5-sqlite \
libqt5svg5-dev \
libqt5x11extras5-dev \
librubberband-dev \
libshout-idjc-dev \
libsndfile1-dev \
libsoundtouch-dev \
libsqlite3-dev \
libssl-dev \
libtag1-dev \
libudev-dev \
libupower-glib-dev \
libusb-1.0-0-dev \
libwavpack-dev \
markdown \
portaudio19-dev \
protobuf-compiler \
qt5keychain-dev \
qtdeclarative5-dev \
qml-module-qtquick-controls \
qml-module-qtquick-controls2 \
qml-module-qt-labs-qmlmodels \
qml-module-qtquick-shapes
