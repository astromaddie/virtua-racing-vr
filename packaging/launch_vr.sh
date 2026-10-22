#!/usr/bin/env bash
set -euo pipefail

package_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)"

if [[ "$(uname -s)" != Linux || "$(uname -m)" != aarch64 ]]; then
    printf 'Virtua Racing Frame build requires Linux/aarch64 (got %s/%s).\n' "$(uname -s)" "$(uname -m)" >&2
    exit 2
fi
if (( $# > 1 )); then
    printf 'Usage: %s [RAW_US_ROM_PATH]\n' "$0" >&2
    exit 2
fi
if [[ ! -x "$package_dir/vrde-vr" ]]; then
    printf 'Game executable is missing: %s/vrde-vr\n' "$package_dir" >&2
    exit 1
fi
if [[ ! -f "$package_dir/rom_preflight.py" ]]; then
    printf 'ROM preflight is missing: %s/rom_preflight.py\n' "$package_dir" >&2
    exit 1
fi
if [[ -z "${HOME:-}" && -z "${XDG_DATA_HOME:-}" ]]; then
    printf 'Set HOME or XDG_DATA_HOME to select a persistent save/settings directory.\n' >&2
    exit 1
fi

caller_dir="$PWD"
data_root="${XDG_DATA_HOME:-$HOME/.local/share}"
if [[ "$data_root" != /* ]]; then
    printf 'XDG_DATA_HOME must be an absolute path: %s\n' "$data_root" >&2
    exit 1
fi
data_dir="$data_root/VirtuaRacingVR"
rom_path="${1:-${VRDE_ROM:-$data_dir/Virtua Racing Deluxe (USA).32x}}"
if [[ "$rom_path" != /* ]]; then
    rom_path="$caller_dir/$rom_path"
fi

if ! mkdir -p -- "$data_dir"; then
    printf 'Could not create persistent data directory: %s\n' "$data_dir" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3 is required to validate the user-supplied ROM.\n' >&2
    exit 1
fi
python3 "$package_dir/rom_preflight.py" "$rom_path"

cd -- "$data_dir"
# One-shot finite diagnostics are opt-in and consumed before the game starts.
# File contains exactly: seconds yaw_degrees dump_emulated_frame.
args=(--vr)
if [[ -f vr-diagnostic.once ]]; then
    read -r seconds yaw dump extra < vr-diagnostic.once
    if [[ ! "$seconds" =~ ^[0-9]+$ || ! "$yaw" =~ ^-?[0-9]+$ || ! "$dump" =~ ^[0-9]+$ || -n "${extra:-}" ]] ||
       (( seconds < 1 || seconds > 300 || yaw < -180 || yaw > 180 || dump < 1 || dump > 16000 )); then
        printf 'Invalid vr-diagnostic.once; expected seconds(1..300) yaw(-180..180) frame(1..16000).\n' >&2
        exit 2
    fi
    rm -- vr-diagnostic.once
    export VRDE_VR_AUTO_EXIT="$seconds" VRDE_VR_FORCE_RENDER=1 VRDE_VR_DIAG_YAW="$yaw" VRDE_VR_DUMP_FRAME="$dump"
    args+=(-p 500:S -p 506: -p 900:S -p 906: -p 1100:S -p 1106: -p 1200:S -p 1206: -p 1700:B)
fi
exec "$package_dir/vrde-vr" "${args[@]}" "$rom_path"
