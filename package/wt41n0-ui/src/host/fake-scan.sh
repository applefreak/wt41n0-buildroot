#!/bin/sh
# The harness's stand-in for wt41n0-scan -v: the scanner answers, then three reads.
echo "scanner: PAACBS00-002-R01" >&2
for c in "0B 4006381333931" "03 WT41N0-ASSET-00172" "01 PALLET-8812"; do
	echo "barcode type 0x${c%% *}" >&2
	echo "${c#* }"
done
exec sleep 60
