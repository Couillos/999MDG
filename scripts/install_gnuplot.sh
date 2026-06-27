#!/usr/bin/env bash
set -euo pipefail

echo "Checking gnuplot installation..."

if command -v gnuplot &>/dev/null; then
    echo "gnuplot is already installed: $(gnuplot --version)"
    exit 0
fi

echo "Installing gnuplot..."
if command -v apt-get &>/dev/null; then
    sudo apt-get update -qq
    sudo apt-get install -y -qq gnuplot
elif command -v brew &>/dev/null; then
    brew install gnuplot
elif command -v dnf &>/dev/null; then
    sudo dnf install -y gnuplot
else
    echo "Please install gnuplot manually: https://gnuplot.info/"
    exit 1
fi

echo "gnuplot installed: $(gnuplot --version)"
