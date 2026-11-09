# Time zone chosen in the Settings app (tz= in /etc/wt41n0/ui.conf, a POSIX TZ string)
TZ=$(sed -n 's/^tz=//p' /etc/wt41n0/ui.conf 2>/dev/null)
[ -n "$TZ" ] && export TZ || unset TZ
