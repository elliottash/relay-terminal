#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Install a Relay .deb on a clean Debian/Ubuntu system (container) and check that it starts.
#
#   smoke-test.sh PATH/TO/relay_*.deb
set -euo pipefail
deb=$(realpath "$1")
export DEBIAN_FRONTEND=noninteractive
sudo=; [[ $(id -u) == 0 ]] || sudo=sudo
$sudo apt-get update -q
$sudo apt-get install -y -q "$deb"
$sudo apt-get install -y -q --no-install-recommends xvfb xauth procps x11-utils

echo "== package metadata"
dpkg-deb -f "$deb" Package Version Depends Recommends
exec bash "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/../smoke-installed.sh"
