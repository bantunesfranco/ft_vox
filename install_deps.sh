#!/bin/bash

# List of packages
packages=(
    "libx11-dev"
    "libdl-dev"
    "libpthread-stubs0-dev"
    "libgl1-mesa-dev"
    "pkg-config"
    "libwayland-dev"
    "libxkbcommon-dev"
    "xorg-dev"
)

# Update package list
sudo apt-get update

# Install packages
for package in "${packages[@]}"; do
    if dpkg-query -Wf'${db:Status-abbrev}' "$package" 2>/dev/null | grep -q '^i'; then
        echo "$package is already installed"
    else
        echo "$package is not installed. Installing..."
        sudo apt-get install -y "$package"
    fi
done