#!/bin/sh
set -eu

limine_version=12.6.1
limine_sha256=1d7f71df1614110892eadb35bad7b7f4277ca0d2dbf8575512da333a546009a7

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
apps_dir=$repo_dir/apps
apps_out_dir=$apps_dir/out
apps_include_dir=$apps_dir/include
apps_lib_dir=$apps_dir/libs
lite_data_dir=$apps_dir/lite-1.11/data
kernel_dir=$repo_dir/kernel
object_dir=$kernel_dir/obj
work_dir=$object_dir/livecd
iso_root=$work_dir/root
payload_dir=$work_dir/payload
initramfs=$iso_root/boot/initramfs.img
limine_dir=${LIMINE_DIR:-$object_dir/limine-$limine_version}
output=${1:-$kernel_dir/plant-os-livecd.iso}

fat_short_path() {
  image=$1
  path=$2
  short=$(mshortname -i "$image" "::/$path")
  short=${short#::/}
  original_leaf=${path##*/}
  short_leaf=${short##*/}
  case "$original_leaf" in
    *.*)
      case "$short_leaf" in
        *.*) ;;
        *)
          extension=${original_leaf##*.}
          upper_extension=$(printf '%s' "$extension" |
            tr '[:lower:]' '[:upper:]')
          case "$short_leaf" in
            *"$upper_extension")
              short_prefix=${short%"$short_leaf"}
              short_base=${short_leaf%"$upper_extension"}
              short=$short_prefix$short_base.$upper_extension
              ;;
          esac
          ;;
      esac
      ;;
  esac
  printf '%s\n' "$short"
}

for command in awk curl du find make mcopy mformat mshortname sha256sum sort \
               tar tr truncate; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "build-livecd: missing required command: $command" >&2
    exit 1
  fi
done

if command -v xorriso >/dev/null 2>&1; then
  iso_maker=xorriso
elif command -v genisoimage >/dev/null 2>&1; then
  iso_maker=genisoimage
else
  echo "build-livecd: install xorriso or genisoimage" >&2
  exit 1
fi

for artifact in "$kernel_dir/boot.img" "$object_dir/kernel.bin" \
                "$apps_out_dir/crti.obj" "$apps_out_dir/doom.bin" \
                "$apps_lib_dir/libtcc1.a" \
                "$apps_dir/tcc/tcc/crti.c" "$kernel_dir/res/doom1.wad"; do
  if [ ! -f "$artifact" ]; then
    echo "build-livecd: missing build artifact: $artifact" >&2
    exit 1
  fi
done

for directory in "$apps_out_dir" "$apps_include_dir" "$apps_lib_dir" \
                 "$lite_data_dir"; do
  if [ ! -d "$directory" ]; then
    echo "build-livecd: missing build directory: $directory" >&2
    exit 1
  fi
done

if [ ! -f "$limine_dir/limine-bios-cd.bin" ]; then
  archive=$object_dir/limine-binary-$limine_version.tar.xz
  mkdir -p "$limine_dir"
  curl -L --fail --show-error \
    "https://github.com/Limine-Bootloader/Limine/releases/download/v$limine_version/limine-binary.tar.xz" \
    -o "$archive"
  printf '%s  %s\n' "$limine_sha256" "$archive" | sha256sum -c -
  tar -xJf "$archive" --strip-components=1 -C "$limine_dir"
fi
make -C "$limine_dir"

rm -rf "$work_dir"
mkdir -p "$payload_dir" "$iso_root/boot/limine"
mcopy -s -i "$kernel_dir/boot.img" '::/*' "$payload_dir"
for resource in boot.bin boot32.bin boot_pfs.bin dosldr.bin; do
  if [ ! -f "$payload_dir/$resource" ]; then
    echo "build-livecd: missing formatting resource: $resource" >&2
    exit 1
  fi
done
rm -f "$payload_dir/kernel.bin" "$payload_dir/setup.mst"
mv "$payload_dir/dosldr.bin" "$payload_dir/DOSLDR.bin"
cp "$object_dir/kernel.bin" "$payload_dir/kernel.bin"

for program in "$apps_out_dir"/*.bin; do
  if [ ! -f "$program" ]; then
    echo "build-livecd: no application binaries found in $apps_out_dir" >&2
    exit 1
  fi
  cp "$program" "$payload_dir/"
done

mkdir -p "$payload_dir/data" "$payload_dir/games" "$payload_dir/tcc/crt" \
         "$payload_dir/tcc/include" "$payload_dir/tcc/inst" \
         "$payload_dir/tcc/lib"
cp -R "$lite_data_dir"/. "$payload_dir/data/"
mv "$payload_dir/doom.bin" "$payload_dir/games/"
rm -f "$payload_dir/doom1.wad"
cp "$kernel_dir/res/doom1.wad" "$payload_dir/games/"
cp -R "$apps_include_dir"/. "$payload_dir/tcc/include/"
cp "$apps_lib_dir"/*.a "$payload_dir/tcc/lib/"
mv "$payload_dir/tcc/lib/libtcc1.a" "$payload_dir/tcc/inst/"
cp "$apps_out_dir/crti.obj" "$payload_dir/tcc/crt/crti.o"
cp "$apps_dir/tcc/tcc/crti.c" "$payload_dir/"

payload_kib=$(du -sk "$payload_dir" | awk '{print $1}')
image_mib=$(((payload_kib + payload_kib / 4 + 2048 + 1023) / 1024))
if [ "$image_mib" -lt 16 ]; then
  image_mib=16
fi
image_sectors=$((image_mib * 2048))
truncate -s 0 "$initramfs"
truncate -s "${image_mib}M" "$initramfs"
mformat -T "$image_sectors" -h 64 -s 16 -i "$initramfs"
mcopy -s -i "$initramfs" "$payload_dir"/* ::/

setup_manifest=$payload_dir/setup.mst
loader_source=$(fat_short_path "$initramfs" DOSLDR.bin)
{
  printf '"files" = [\n'
  printf '    {"type" = "file" "source" = "%s" "path" = "DOSLDR.bin" "fat" = "%s"}' \
    "$loader_source" "$loader_source"
  find "$payload_dir" -mindepth 1 -type d -printf '%P\n' | sort |
    while IFS= read -r path; do
      fat_path=$(fat_short_path "$initramfs" "$path")
      printf ',\n    {"type" = "dir" "path" = "%s" "fat" = "%s"}' \
        "$path" "$fat_path"
    done
  find "$payload_dir" -type f ! -name DOSLDR.bin ! -name setup.mst \
      -printf '%P\n' | sort |
    while IFS= read -r path; do
      source=$(fat_short_path "$initramfs" "$path")
      printf ',\n    {"type" = "file" "source" = "%s" "path" = "%s" "fat" = "%s"}' \
        "$source" "$path" "$source"
    done
  printf ',\n    {"type" = "file" "source" = "SETUP.MST" "path" = "setup.mst" "fat" = "SETUP.MST"}\n]\n'
} >"$setup_manifest"
mcopy -i "$initramfs" "$setup_manifest" ::/setup.mst

cp "$object_dir/kernel.bin" "$iso_root/boot/kernel.bin"
cp "$kernel_dir/res/limine.conf" "$iso_root/limine.conf"
cp "$limine_dir/limine-bios-cd.bin" "$iso_root/boot/limine/"
cp "$limine_dir/limine-bios.sys" "$iso_root/boot/limine/"
cp "$limine_dir/LICENSE" "$iso_root/boot/limine/"

if [ "$iso_maker" = xorriso ]; then
  xorriso -as mkisofs \
    -R -J -joliet-long -iso-level 3 -V PLANT_OS_LIVE \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o "$output" "$iso_root"
else
  genisoimage \
    -R -J -joliet-long -iso-level 3 -V PLANT_OS_LIVE \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o "$output" "$iso_root"
fi
"$limine_dir/limine" bios-install --force "$output"

echo "Plant OS LiveCD: $output"
