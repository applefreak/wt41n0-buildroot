# An expired root password (last change 0 in /etc/shadow: the image's default one,
# see BR2_WT41N0_ROOT_PASSWD_EXPIRED) must be changed at the first ssh login, as on
# many Linux installs. Dropbear and BusyBox login do not do this themselves.
if [ -n "$SSH_CONNECTION" ] && [ "$(id -u)" = 0 ] && [ -t 0 ] &&
   [ "$(awk -F: '$1 == "root" { print $3 }' /etc/shadow 2>/dev/null)" = 0 ]; then
	echo
	echo "This unit still has the default root password. Choose a new one now"
	echo "(the session ends if it is not changed)."
	echo
	trap 'echo; exit 1' INT QUIT TSTP
	if ! passwd root; then
		echo "The password was not changed; logging out."
		exit 1
	fi
	trap - INT QUIT TSTP
	echo "Password changed."
fi
