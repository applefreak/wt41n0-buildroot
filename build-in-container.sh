#!/bin/bash
# Convenience wrapper: run Buildroot for the WT41N0 inside a Linux container
# (Buildroot does not build on macOS). Any Debian/Ubuntu container that has
# this tree mounted works; the paths below can be overridden.
#
#   docker exec <container> bash -lc '/src/buildroot/build-in-container.sh [make targets]'
#
# No targets = full build. "defconfig" as the first target reloads
# configs/wt41n0_defconfig. Other examples: linux-rebuild all, menuconfig
# (needs docker exec -it), savedefconfig, clean.
#
# Buildroot's source, output and download cache are kept outside the mounted
# tree, on the container's own (case-sensitive) filesystem. The images are
# copied to $EXPORT_DIR at the end.
set -euo pipefail

EXT=$(cd "$(dirname "$0")" && pwd)
BR_SRC=${BR_SRC:-/opt/buildroot}
BR_OUT=${BR_OUT:-/opt/br-output}
EXPORT_DIR=${EXPORT_DIR:-$EXT/images}
export BR2_DL_DIR=${BR2_DL_DIR:-/opt/br-dl}
BR_BRANCH=${BR_BRANCH:-2025.02.x}		# Buildroot LTS branch the BSP is developed against

need=""
for t in git rsync unzip python3 bc cpio file patch perl wget g++ make; do
	command -v "$t" >/dev/null || need="$need $t"
done
if [ -n "$need" ]; then
	echo "== installing host tools:$need"
	apt-get update -q >/dev/null
	apt-get install -y -q git rsync unzip python3 bc cpio file patch perl wget \
		build-essential libncurses-dev ca-certificates >/dev/null
fi

if [ ! -d "$BR_SRC/.git" ]; then
	echo "== cloning Buildroot branch $BR_BRANCH"
	git clone -q --depth 1 --branch "$BR_BRANCH" https://gitlab.com/buildroot.org/buildroot.git "$BR_SRC"
fi
echo "== Buildroot $(git -C "$BR_SRC" describe --tags --always 2>/dev/null) in $BR_SRC"

# Optionally reuse a kernel tarball that is already around (saves a 150 MB download)
mkdir -p "$BR2_DL_DIR/linux"
if [ -n "${KERNEL_TARBALL:-}" ] && [ -f "$KERNEL_TARBALL" ] && [ ! -f "$BR2_DL_DIR/linux/$(basename "$KERNEL_TARBALL")" ]; then
	cp "$KERNEL_TARBALL" "$BR2_DL_DIR/linux/"
fi

# Containers usually run as root, which some host packages refuse to configure as.
[ "$(id -u)" = 0 ] && export FORCE_UNSAFE_CONFIGURE=1

MAKE="make -C $BR_SRC O=$BR_OUT BR2_EXTERNAL=$EXT"
targets=("$@")
if [ ! -f "$BR_OUT/.config" ] || [ "${targets[0]:-}" = defconfig ]; then
	echo "== loading wt41n0_defconfig"
	$MAKE wt41n0_defconfig
	[ "${targets[0]:-}" = defconfig ] && targets=("${targets[@]:1}")
	# Buildroot silently drops options it does not know: make that loud.
	missing=0
	for opt in $(sed -n 's/^\(BR2_[A-Z0-9_]*\)=y$/\1/p' "$EXT/configs/wt41n0_defconfig"); do
		grep -q "^$opt=y" "$BR_OUT/.config" || { echo "WARNING: $opt is not set by this Buildroot version"; missing=1; }
	done
	[ $missing = 0 ] && echo "== all defconfig options accepted"
fi

[ ${#targets[@]} -gt 0 ] || targets=(all)
echo "== make ${targets[*]}"
$MAKE "${targets[@]}"

if [ -d "$BR_OUT/images" ]; then
	mkdir -p "$EXPORT_DIR"
	cp -f "$BR_OUT"/images/{zImage,omap4-wt41n0.dtb,rootfs.cpio.gz,wt41n0-loader.bin} "$EXPORT_DIR"/ 2>/dev/null || true
	cp -f "$BR_OUT"/images/wt41n0-linux.* "$BR_OUT"/images/wt41n0-emmc.* "$BR_OUT"/images/wt41n0-install.* "$EXPORT_DIR"/ 2>/dev/null || true
	cp -f "$BR_OUT"/images/rootfs.ext2.gz "$EXPORT_DIR"/rootfs.ext4.gz 2>/dev/null || true
	echo "== images copied to $EXPORT_DIR"; ls -la "$EXPORT_DIR"
fi
