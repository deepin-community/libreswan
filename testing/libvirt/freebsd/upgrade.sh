#!/bin/sh

set -xe ; exec < /dev/null

# Download /usr/local/etc/pkg.conf so it can be edited;
# IGNORE_OSVERSION=yes is to stop PKG complaining that there is a
# newer version of pkg (see github/846).

IGNORE_OSVERSION=yes pkg bootstrap -y

# Enable the cache

mkdir -p /pool/pkg.freebsd
ed <<EOF /usr/local/etc/pkg.conf
/PKG_CACHEDIR/ s;.*PKG_CACHEDIR.*;PKG_CACHEDIR = "/pool/pkg.freebsd";
w
q
EOF

# install dependencies

pkg install -y gmake
pkg install -y git
pkg install -y pkgconf # aka pkg-config; grrr
pkg install -y nss
pkg install -y libevent
pkg install -y unbound
pkg install -y bison
pkg install -y flex
pkg install -y ldns
pkg install -y xmlto
pkg install -y bash
pkg install -y strongswan
pkg install -y fping
pkg install -y gdb
pkg install -y gcc
