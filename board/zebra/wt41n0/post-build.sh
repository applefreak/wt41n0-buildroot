#!/bin/sh
# Buildroot post-build script for the WT41N0: finish the target tree.
#   $1 = target directory. BR2_CONFIG and HOST_DIR come from Buildroot.
set -eu
TARGET=$1
BOARD=$(cd "$(dirname "$0")" && pwd)
EXT=$(cd "$BOARD/../../.." && pwd)

cfg_bool() { grep -q "^$1=y" "$BR2_CONFIG"; }
cfg_str() {	# string option, with $(BR2_EXTERNAL_WT41N0_PATH) expanded
	sed -n "s/^$1=\"\(.*\)\"$/\1/p" "$BR2_CONFIG" | sed "s|\$(BR2_EXTERNAL_WT41N0_PATH)|$EXT|g"
}

# Boot verbosity for the start-up scripts (the kernel side is in post-image.sh)
if cfg_bool BR2_WT41N0_BOOT_QUIET; then verbosity=quiet; else verbosity=verbose; fi
sed -i "s/^BOOT_VERBOSITY=.*/BOOT_VERBOSITY=$verbosity/" "$TARGET/etc/default/wt41n0"

# /boot on the eMMC root holds the recovery flag (see emmc-init)
mkdir -p "$TARGET/boot"

# MPD (/etc/mpd.conf): its database and state, and the music library
mkdir -p "$TARGET/var/lib/mpd" "$TARGET/root/Music/Playlists"

# The launcher instead of the shell on the LCD (tty1). The overlay brings a fresh
# inittab at every build, so turning the option off restores the shell.
if cfg_bool BR2_WT41N0_LAUNCHER; then
	sed -i 's|^tty1::respawn:-/bin/sh$|tty1::respawn:/usr/bin/wt41n0-launcher|' "$TARGET/etc/inittab"
	grep -q '^tty1::respawn:/usr/bin/wt41n0-launcher$' "$TARGET/etc/inittab" ||
		{ echo "post-build: could not put the launcher in inittab" >&2; exit 1; }
fi

# Console keymap for the keypad, generated from the one table in support/keymap.py
python3 "$EXT/support/keymap.py" --bmap > "$TARGET/etc/wt41n0.bmap"

# Private files from the local directory, when present
LOCAL=$(cfg_str BR2_WT41N0_LOCAL_DIR)

# Wi-Fi: by default a configuration with no networks; the Settings app adds them
# on the unit (update_config=1 lets wpa_supplicant save them). local/wifi.conf only
# goes in with BR2_WT41N0_LOCAL_WIFI. The wpa_supplicant package ships a sample
# file that would join any open network; never keep that one.
rm -f "$TARGET/etc/wpa_supplicant.conf"
if cfg_bool BR2_WT41N0_LOCAL_WIFI && [ -s "$LOCAL/wifi.conf" ]; then
	install -m 0600 "$LOCAL/wifi.conf" "$TARGET/etc/wpa_supplicant.conf"
else
	cfg_bool BR2_WT41N0_LOCAL_WIFI &&
		echo "post-build: BR2_WT41N0_LOCAL_WIFI is set but $LOCAL/wifi.conf is missing; no networks" >&2
	[ -s "$LOCAL/wifi.conf" ] && ! cfg_bool BR2_WT41N0_LOCAL_WIFI &&
		echo "post-build: not using $LOCAL/wifi.conf (BR2_WT41N0_LOCAL_WIFI is off)" >&2
	printf 'ctrl_interface=/var/run/wpa_supplicant\n' > "$TARGET/etc/wpa_supplicant.conf"
	chmod 0600 "$TARGET/etc/wpa_supplicant.conf"
fi
grep -q '^update_config=' "$TARGET/etc/wpa_supplicant.conf" ||
	sed -i '1a update_config=1' "$TARGET/etc/wpa_supplicant.conf"

# ssh: by default root logs in with the root password (BR2_TARGET_GENERIC_ROOT_PASSWD;
# dropbear refuses empty ones), and dropbear makes the unit's own host key at its
# first start (kept on the eMMC root; from RAM, a new one each boot). The keys in
# local/ go in only with BR2_WT41N0_LOCAL_SSH.
rm -rf "$TARGET/root/.ssh"
rm -f "$TARGET/etc/dropbear_ed25519_host_key" "$TARGET/etc/default/dropbear"
if cfg_bool BR2_WT41N0_LOCAL_SSH; then
	if [ -s "$LOCAL/authorized_keys" ]; then
		install -d -m 0700 "$TARGET/root/.ssh"
		install -m 0600 "$LOCAL/authorized_keys" "$TARGET/root/.ssh/authorized_keys"
	fi
	# A fixed host key: the same ssh identity on every unit and at every boot
	if [ -s "$LOCAL/dropbear_ed25519_host_key" ]; then
		install -m 0600 "$LOCAL/dropbear_ed25519_host_key" "$TARGET/etc/dropbear_ed25519_host_key"
		echo 'DROPBEAR_ARGS="-r /etc/dropbear_ed25519_host_key"' > "$TARGET/etc/default/dropbear"
	fi
elif [ -s "$LOCAL/authorized_keys" ] || [ -s "$LOCAL/dropbear_ed25519_host_key" ]; then
	echo "post-build: not using the ssh keys in $LOCAL (BR2_WT41N0_LOCAL_SSH is off)" >&2
fi

# A default root password is marked expired (last change = day 0): the first ssh
# login has to choose a new one (/etc/profile.d/root-password.sh). Buildroot has
# set the password by now (its finalize step runs before this script).
if cfg_bool BR2_WT41N0_ROOT_PASSWD_EXPIRED && grep -q '^root:[^:!*][^:]*:' "$TARGET/etc/shadow"; then
	sed -i 's/^\(root:[^:]*\):[^:]*:/\1:0:/' "$TARGET/etc/shadow"
fi
if ! grep -q '^root:[^:!*][^:]*:' "$TARGET/etc/shadow" && [ ! -s "$TARGET/root/.ssh/authorized_keys" ]; then
	echo "post-build: no root password and no ssh key: nobody can log in over ssh" >&2
fi

# Firmware. The linux-firmware package installs every WL128x variant; the driver
# only loads the "fw-5" single-role and multi-role images and the WL128x NVS file
# (fw-5-plt is for factory test mode).
FW="$TARGET/lib/firmware/ti-connectivity"
if [ -d "$FW" ]; then
	find "$FW" -type f ! -name 'wl128x-fw-5-sr.bin' ! -name 'wl128x-fw-5-mr.bin' \
		! -name 'wl128x-nvs.bin' ! -name 'TIInit_*.bts' -delete
fi
# Extra firmware from local/firmware/ (same layout as /lib/firmware), e.g. the
# Bluetooth init script ti-connectivity/TIInit_10.6.15.bts, which linux-firmware
# does not ship.
if [ -d "$LOCAL/firmware" ]; then
	mkdir -p "$TARGET/lib/firmware"
	cp -R "$LOCAL/firmware/." "$TARGET/lib/firmware/"
	find "$TARGET/lib/firmware" -name .DS_Store -delete	# macOS Finder files
fi

# Size: the image has to stay below 15.2 MB (docs/boot-flow.md).
# Kernel modules: every driver the board uses is built in; the modules are
# drivers for other OMAP boards.
rm -rf "$TARGET/lib/modules"
# Developer tools that packages install alongside their libraries.
for f in gi-compile-repository gi-decompile-typelib gi-inspect-typelib pcre2grep pcre2test \
		dbus-test-tool; do
	rm -f "$TARGET/usr/bin/$f"
done
# Toolchain runtimes the prebuilt toolchain copies whether or not anything uses
# them (C++, Fortran, OpenMP). Remove each one that no program in the image
# links against, so adding a C++ package keeps working. In an existing output
# directory a removed one does not come back by itself: after adding the first C++
# program, run "make toolchain-external-arm-arm-reinstall all".
READELF=$(ls "$HOST_DIR"/bin/*-linux-gnueabihf-readelf 2>/dev/null | head -1)
if [ -x "$READELF" ]; then
	needed=$(find "$TARGET" -type f \( -perm -u+x -o -name '*.so*' \) -exec "$READELF" -d {} + 2>/dev/null |
		sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort -u)
	for lib in libstdc++.so.6 libgfortran.so.5 libgomp.so.1 libatomic.so.1 libasan.so.8 libubsan.so.1 \
			libgirepository-2.0.so.0; do
		echo "$needed" | grep -qx "$lib" && continue
		find "$TARGET/lib" "$TARGET/usr/lib" -maxdepth 1 -name "${lib%%.so*}.so*" -exec rm -f {} +
	done
fi
