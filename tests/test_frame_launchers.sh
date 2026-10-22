#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$tmp/bin" "$tmp/package" "$tmp/caller dir" "$tmp/data root"
cp "$root/packaging/launch_flat.sh" "$tmp/package/launch-flat.sh"
cp "$root/packaging/launch_openxr_probe.sh" "$tmp/package/launch-openxr-probe.sh"
cp "$root/tools/rom_preflight.py" "$tmp/package/rom_preflight.py"
cat > "$tmp/bin/uname" <<'EOF'
#!/usr/bin/env sh
case "$1" in
    -s) printf '%s\n' Linux ;;
    -m) printf '%s\n' "${TEST_UNAME_MACHINE:-aarch64}" ;;
    *) exit 2 ;;
esac
EOF
cat > "$tmp/bin/python3" <<'EOF'
#!/usr/bin/env sh
printf '%s\n' "$@" >> "$PRECHECK_LOG"
EOF
cat > "$tmp/package/vrde" <<'EOF'
#!/usr/bin/env sh
printf '%s\n' "$PWD" > "$GAME_LOG"
printf '%s\n' "$@" >> "$GAME_LOG"
EOF
cat > "$tmp/package/openxr-colour-probe" <<'EOF'
#!/usr/bin/env sh
printf '%s\n' "$PWD" > "$PROBE_LOG"
printf '%s\n' "$@" >> "$PROBE_LOG"
EOF
chmod +x "$tmp/bin/uname" "$tmp/bin/python3" "$tmp/package/launch-flat.sh" \
    "$tmp/package/launch-openxr-probe.sh" "$tmp/package/vrde" "$tmp/package/openxr-colour-probe"

export PATH="$tmp/bin:$PATH"
export HOME="$tmp/home with spaces"
export XDG_DATA_HOME="$tmp/data root"
export GAME_LOG="$tmp/game.log"
export PROBE_LOG="$tmp/probe.log"
export PRECHECK_LOG="$tmp/precheck.log"
rom="$tmp/caller dir/rom image.32x"
touch "$rom"

(cd "$tmp/caller dir" && "$tmp/package/launch-flat.sh" "rom image.32x") || fail "flat launcher failed"
[[ "$(sed -n '1p' "$GAME_LOG")" == "$XDG_DATA_HOME/VirtuaRacingVR" ]] || fail "flat launcher did not use persistent data directory"
[[ "$(sed -n '2p' "$GAME_LOG")" == "$rom" ]] || fail "flat launcher changed the ROM path"
[[ "$(sed -n '2p' "$PRECHECK_LOG")" == "$rom" ]] || fail "flat launcher skipped or changed ROM preflight"

precheck_lines="$(wc -l < "$PRECHECK_LOG")"
(cd "$tmp/caller dir" && "$tmp/package/launch-openxr-probe.sh" 4.5) || fail "probe launcher failed"
[[ "$(sed -n '2p' "$PROBE_LOG")" == 4.5 ]] || fail "probe duration was not forwarded"
[[ "$(wc -l < "$PRECHECK_LOG")" == "$precheck_lines" ]] || fail "probe launcher invoked ROM preflight"

if TEST_UNAME_MACHINE=x86_64 "$tmp/package/launch-flat.sh" "$rom" >"$tmp/unsafe.out" 2>&1; then
    fail "flat launcher allowed a non-ARM64 host"
fi
rg -q 'requires Linux/aarch64' "$tmp/unsafe.out" || fail "non-ARM64 rejection was unclear"

mkdir -p "$tmp/missing-game"
cp "$tmp/package/launch-flat.sh" "$tmp/missing-game/launch-flat.sh"
cp "$tmp/package/rom_preflight.py" "$tmp/missing-game/rom_preflight.py"
if "$tmp/missing-game/launch-flat.sh" "$rom" >"$tmp/missing-game.out" 2>&1; then
    fail "flat launcher accepted a missing game executable"
fi
rg -q 'Game executable is missing' "$tmp/missing-game.out" || fail "missing game error was unclear"

mkdir -p "$tmp/missing-probe"
cp "$tmp/package/launch-openxr-probe.sh" "$tmp/missing-probe/launch-openxr-probe.sh"
if "$tmp/missing-probe/launch-openxr-probe.sh" >"$tmp/missing-probe.out" 2>&1; then
    fail "probe launcher accepted a missing probe executable"
fi
rg -q 'OpenXR colour probe is missing' "$tmp/missing-probe.out" || fail "missing probe error was unclear"

printf 'Frame launcher checks passed.\n'
