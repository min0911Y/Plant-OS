#!/bin/sh
set -eu

image="${ALPINE_IMAGE:-docker.io/library/alpine:edge}"
mirror="${ALPINE_MIRROR:-https://mirror.sjtu.edu.cn/alpine}"
default_packages="coreutils bash gcc musl-dev tmux"
packages="${ALPINE_PACKAGES:-$default_packages}"
out="${1:-kernel64/build/alpine-rootfs.tar.zst}"

container_script=$(cat << EOF
set -eu
ver="\$(cut -d. -f1,2 /etc/alpine-release)"
repo="${mirror}/v\$ver"
printf "%s\n%s\n" "\$repo/main" "\$repo/community" > /etc/apk/repositories
apk update
apk add --no-cache ${packages}
EOF
)

mkdir -p "$(dirname "$out")"
cid="$(podman create "$image" /bin/sh -c "$container_script")"
trap 'podman rm -f "$cid" >/dev/null 2>&1 || true' EXIT
podman start -a "$cid"
podman export -o "${out%.zst}" "$cid"
zstd -19 --rm "${out%.zst}" -o "$out"
