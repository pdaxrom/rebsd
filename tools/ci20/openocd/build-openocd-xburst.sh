#!/bin/sh

set -eu

OPENOCD_REPOSITORY=${OPENOCD_REPOSITORY:-https://github.com/Ingenic-community/OpenOCD-XBurst.git}
OPENOCD_COMMIT=e73c62f593dfba5631a767826cb07a641c4ee8ec
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_ROOT=${OPENOCD_BUILD_ROOT:-"$SCRIPT_DIR/build"}
SOURCE_DIR="$BUILD_ROOT/OpenOCD-XBurst"

if [ ! -d "$SOURCE_DIR/.git" ]; then
    mkdir -p "$BUILD_ROOT"
    git clone "$OPENOCD_REPOSITORY" "$SOURCE_DIR"
fi

git -C "$SOURCE_DIR" fetch origin v0.10.0
git -C "$SOURCE_DIR" checkout --detach "$OPENOCD_COMMIT"
git -C "$SOURCE_DIR" reset --hard "$OPENOCD_COMMIT"
git -C "$SOURCE_DIR" clean -dffx

# Only Jim Tcl is needed for this build.  libjaylink is disabled below.
git -C "$SOURCE_DIR" -c url.https://repo.or.cz/.insteadOf=http://repo.or.cz/ \
    submodule update --init jimtcl

git -C "$SOURCE_DIR" apply "$SCRIPT_DIR/patches/0001-ci20-xburst-jtag.patch"

(
    cd "$SOURCE_DIR"
    ./bootstrap nosubmodule

    # Old Jim Tcl's generated wrapper can contain "# !/bin/sh" with newer
    # autotools.  Correct only that known generated-file defect.
    if [ "$(sed -n '1p' jimtcl/configure)" = "# !/bin/sh" ]; then
        sed -i.bak '1s/^# !\//#\!\//g' jimtcl/configure
        rm -f jimtcl/configure.bak
    fi

    CCACHE=none ./configure \
        --enable-ftdi \
        --disable-internal-libjaylink \
        --disable-werror \
        --disable-doxygen-html
    CCACHE=none make -j"${OPENOCD_JOBS:-4}"
)

echo "$SOURCE_DIR/src/openocd"
