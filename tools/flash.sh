#!/usr/bin/env bash
# Flash firmware/build/elrs_sim_rx.uf2 onto an RP2040-Zero.
#
# Works from any of these starting states:
#   1. Board already in BOOTSEL (RPI-RP2 drive mounted, or USB 2e8a:0003 present)
#   2. Board running this firmware: its serial port is "touched" at 1200 baud,
#      which the firmware treats as "reboot into BOOTSEL"
#
# Then copies the UF2 to the RPI-RP2 drive if the desktop auto-mounted it, or
# falls back to picotool inside the build container with USB passed through.
#
# Usage: tools/flash.sh [path/to/file.uf2] [--port /dev/ttyACM0]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

UF2="firmware/build/elrs_sim_rx.uf2"
PORT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        *.uf2)  UF2="$1"; shift ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

[ -f "$UF2" ] || { echo "No UF2 at $UF2. Run tools/build.sh first." >&2; exit 1; }

in_bootsel() {
    lsusb 2>/dev/null | grep -qi "2e8a:0003"
}

find_mount() {
    # RPI-RP2 auto-mount point, if the desktop mounted it
    for d in /media/"$USER"/RPI-RP2 /run/media/"$USER"/RPI-RP2 /media/RPI-RP2 /mnt/RPI-RP2; do
        [ -d "$d" ] && [ -w "$d" ] && { echo "$d"; return 0; }
    done
    findmnt -rn -o TARGET -S LABEL=RPI-RP2 2>/dev/null | head -1
}

find_port() {
    [ -n "$PORT" ] && { echo "$PORT"; return; }
    ls /dev/serial/by-id/*ELRS_Sim_RX* 2>/dev/null | head -1 || true
    ls /dev/ttyACM* 2>/dev/null | head -1 || true
}

if ! in_bootsel; then
    port="$(find_port | head -1)"
    if [ -n "$port" ] && [ -e "$port" ]; then
        echo ">> Asking $port to reboot into BOOTSEL (1200 baud touch)"
        stty -F "$port" 1200 2>/dev/null || true
        # give the board a moment to drop off and re-enumerate
        for _ in $(seq 1 30); do
            in_bootsel && break
            sleep 0.2
        done
    fi
fi

if ! in_bootsel; then
    echo "Board not found in BOOTSEL." >&2
    echo "Hold BOOT on the RP2040-Zero, plug it in, release, then re-run." >&2
    exit 1
fi

# Wait briefly for an auto-mount
mnt=""
for _ in $(seq 1 15); do
    mnt="$(find_mount || true)"
    [ -n "$mnt" ] && break
    sleep 0.2
done

if [ -n "$mnt" ]; then
    echo ">> Copying $UF2 to $mnt"
    cp "$UF2" "$mnt/"
    sync
    echo ">> Done. Board is rebooting into the new firmware."
    exit 0
fi

echo ">> No RPI-RP2 mount found, using picotool in the container"
IMAGE="${ELRS_SIM_RX_IMAGE:-elrs-sim-rx-build}"
docker run --rm --privileged \
    -v /dev/bus/usb:/dev/bus/usb \
    -v "$PWD":/work -w /work \
    "$IMAGE" picotool load -v -x "$UF2"
echo ">> Done."
