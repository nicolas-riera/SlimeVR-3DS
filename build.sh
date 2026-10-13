#!/usr/bin/env bash
# Builds dist/SlimeVR-3DS.{3dsx,cia,3ds}.
# Uses $DEVKITPRO (default /opt/devkitpro). makerom and bannertool must be in
# PATH, or in $CTRTOOLS.
set -e
cd "$(dirname "$0")"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
if [ -n "$CTRTOOLS" ]; then export PATH="$CTRTOOLS:$PATH"; fi
make -j"$(nproc)" "$@"
