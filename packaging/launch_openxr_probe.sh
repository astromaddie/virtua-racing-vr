#!/usr/bin/env bash
set -euo pipefail

package_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)"

if [[ "$(uname -s)" != Linux || "$(uname -m)" != aarch64 ]]; then
    printf 'OpenXR colour probe requires Linux/aarch64 (got %s/%s).\n' "$(uname -s)" "$(uname -m)" >&2
    exit 2
fi
if (( $# > 1 )); then
    printf 'Usage: %s [SECONDS]\n' "$0" >&2
    exit 2
fi
if [[ ! -x "$package_dir/openxr-colour-probe" ]]; then
    printf 'OpenXR colour probe is missing: %s/openxr-colour-probe\n' "$package_dir" >&2
    exit 1
fi

exec "$package_dir/openxr-colour-probe" "${1:-8}"
