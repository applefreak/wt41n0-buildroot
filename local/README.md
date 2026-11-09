# local/

Files in this directory stay on your machine: everything here except this
README is ignored by git. The build picks them up when they exist.

| File | Purpose |
|---|---|
| `wifi.conf` | A `wpa_supplicant.conf` with your network, installed as `/etc/wpa_supplicant.conf` only when `BR2_WT41N0_LOCAL_WIFI` is on (off by default; networks are otherwise added on the unit in Settings). |
| `authorized_keys` | ssh public keys allowed to log in as root, only with `BR2_WT41N0_LOCAL_SSH` (off by default; ssh then uses the root password). |
| `dropbear_ed25519_host_key` | A fixed ssh host key, so the unit keeps its identity across boots and rebuilds, only with `BR2_WT41N0_LOCAL_SSH`. Create one with `dropbearkey -t ed25519 -f dropbear_ed25519_host_key`. |
| `firmware/` | Extra firmware, copied to `/lib/firmware`. **Bluetooth** needs `firmware/ti-connectivity/TIInit_10.6.15.bts`: copy `tiinit_10.6.15.bts` from your unit's Windows CE image (the `\Windows` folder of the OS image) and rename it. TI's public file of the same name does **not** work on this unit, and its failure also crashes Wi-Fi. Without the file the build switches Bluetooth off in the device tree; everything else works. |

Example `wifi.conf`:

```
ctrl_interface=/var/run/wpa_supplicant
network={
    ssid="my-network"
    psk="my-passphrase"
}
```
