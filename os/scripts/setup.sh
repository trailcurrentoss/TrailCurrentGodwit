#!/usr/bin/env bash
# Fetch AGL and create a build directory for the Radxa Dragon Q6A.
#
#   scripts/setup.sh [AGL features...]      default features: agl-demo agl-devel godwit
#
# Then:  source agl/build-dragon-q6a/agl-init-build-env
#        bitbake godwit-image                (Godwit head unit)
#        bitbake agl-image-weston            (bring-up image)
set -euo pipefail

TOP=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
AGL_BRANCH=${AGL_BRANCH:-vimba}
AGL_MANIFEST=${AGL_MANIFEST:-vimba_22.0.0.xml}
AGL_DIR=${AGL_DIR:-$TOP/agl}
BUILD_DIR=${BUILD_DIR:-build-dragon-q6a}
FEATURES=${*:-agl-demo agl-devel godwit}

REPO=$TOP/tools/repo
if [ ! -x "$REPO" ]; then
    mkdir -p "$TOP/tools"
    curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o "$REPO"
    chmod +x "$REPO"
fi

mkdir -p "$AGL_DIR"
cd "$AGL_DIR"
if [ ! -d .repo ]; then
    "$REPO" init -u https://gerrit.automotivelinux.org/gerrit/AGL/AGL-repo \
        -b "$AGL_BRANCH" -m "$AGL_MANIFEST"
fi
"$REPO" sync -j"$(nproc)" --no-clone-bundle

ln -sfn "$TOP/meta-dragon-q6a" "$AGL_DIR/meta-dragon-q6a"
ln -sfn "$TOP/meta-godwit" "$AGL_DIR/meta-godwit"

# aglsetup.sh is meant to be sourced and reads unset variables
set +eu
# shellcheck disable=SC1091
source meta-agl/scripts/aglsetup.sh -m dragon-q6a -b "$BUILD_DIR" $FEATURES
