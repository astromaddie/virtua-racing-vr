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
if [[ ! -x "$package_dir/vrde" ]]; then
    printf 'Game executable is missing: %s/vrde\n' "$package_dir" >&2
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
exec "$package_dir/vrde" "$rom_path"
