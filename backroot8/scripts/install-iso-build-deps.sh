#!/usr/bin/env bash
# Host packages needed to build and pack Backroot 8 ISOs (Ubuntu/Debian).
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo "$0" "$@"
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
    squashfs-tools \
    grub-pc-bin \
    grub-efi-amd64-bin \
    xorriso \
    mtools \
    arch-install-scripts \
    zstd \
    curl \
    unzip \
    libcairo2-dev \
    libpoppler-glib-dev \
    libfreetype-dev \
    libx11-dev \
    libxft-dev \
    pkg-config

echo "[backroot8] ISO build dependencies installed."
