#!/bin/sh

set -xe

echo disable useless repos

for repo in fedora-modular updates-modular fedora-cisco-openh264 ; do
    echo disabling: ${repo}
    dnf config-manager --set-disable ${repo}
done

echo enable useful repos

for repo in fedora-debuginfo updates-debuginfo ; do
    echo enabling: ${repo}
    dnf config-manager --set-enable ${repo}
done

# Point the cache at /pool
dnf config-manager --save --setopt=keepcache=True
# not fedora.dnf which matches: rm -f /pool/fedora.*
dnf config-manager --save --setopt=cachedir=/pool/dnf.fedora
#dnf config-manager --save --setopt=makecache=0

# make it explicit
dnf makecache

exec dnf install -y "$@"
