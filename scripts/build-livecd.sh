#!/bin/sh
set -eu

limine_version=12.6.1
limine_sha256=1d7f71df1614110892eadb35bad7b7f4277ca0d2dbf8575512da333a546009a7

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
apps_dir=$repo_dir/apps
architecture=${2:-i386}
apps_out_dir=$apps_dir/out
apps_include_dir=$apps_dir/include
apps_lib_dir=$apps_dir/libs
lite_data_dir=$apps_dir/lite-1.11/data
kernel_dir=$repo_dir/kernel
object_dir=$kernel_dir/obj
case "$architecture" in
  i386) ;;
  x86_64)
    apps_out_dir=$apps_out_dir/x86_64
    apps_lib_dir=$apps_lib_dir/x86_64
    object_dir=$object_dir/x86_64
    ;;
  *) echo "build-livecd: unsupported architecture: $architecture" >&2; exit 1 ;;
esac
work_dir=$object_dir/livecd
iso_root=$work_dir/root
payload_dir=$work_dir/payload
initramfs=$work_dir/initramfs.img
limine_dir=${LIMINE_DIR:-$kernel_dir/obj/limine-$limine_version}
output=${1:-$kernel_dir/plant-os-livecd.iso}
openjdk_payload_dir=$work_dir/openjdk-payload
openjdk_disk=
if [ -n "${PLANT_OPENJDK_DIR:-}" ]; then
  openjdk_disk=${PLANT_OPENJDK_DISK:-${output%.iso}-jdk.img}
fi

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

for command in awk curl dirname du find gzip make mcopy mformat mshortname \
               objcopy sha256sum sort tar tr truncate; do
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

if [ "$architecture" = i386 ]; then
for artifact in "$kernel_dir/boot.img" "$object_dir/kernel.bin" \
                "$apps_out_dir/crti.obj" "$apps_out_dir/doom.bin" \
                "$apps_lib_dir/libtcc1.a" "$apps_out_dir/sdk-libraries.list" \
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

else
  for artifact in "$object_dir/kernel.bin" "$apps_out_dir/init.bin" "$apps_out_dir/psh.bin"; do
    if [ ! -f "$artifact" ]; then
      echo "build-livecd: missing build artifact: $artifact" >&2
      exit 1
    fi
  done
fi

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
if [ "$architecture" = i386 ]; then
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
else
  cp "$object_dir"/*.mod "$payload_dir/"
  cp "$kernel_dir/res/init.mst" "$kernel_dir/res/env.cfg" "$kernel_dir/res/sys.cfg" "$payload_dir/"
  cp "$repo_dir/font/font.bin" "$repo_dir/font/HZK16" "$kernel_dir/res/font.ttf" "$payload_dir/"
fi

while IFS= read -r program; do
  cp "$apps_out_dir/$program" "$payload_dir/"
done < "$apps_out_dir/applications.list"

mkdir -p "$payload_dir/data" "$payload_dir/games" "$payload_dir/lib"
cp -R "$lite_data_dir"/. "$payload_dir/data/"
mv "$payload_dir/doom.bin" "$payload_dir/games/"
rm -f "$payload_dir/doom1.wad"
cp "$kernel_dir/res/doom1.wad" "$payload_dir/games/"
cp -R "$apps_out_dir/lib"/. "$payload_dir/lib/"

if [ -n "${PLANT_OPENJDK_DIR:-}" ]; then
  if [ "$architecture" != x86_64 ] || [ ! -d "$PLANT_OPENJDK_DIR" ]; then
    echo "build-livecd: PLANT_OPENJDK_DIR requires an x86_64 JDK directory" >&2
    exit 1
  fi
  mkdir -p "$openjdk_payload_dir/java"
  cp -RL "$PLANT_OPENJDK_DIR"/. "$openjdk_payload_dir/java/"
  mkdir -p "$openjdk_payload_dir/java/lib"
  for library in libp.so libcpp.so libm.so.6 libz.so.1; do
    if [ ! -f "$payload_dir/lib/$library" ]; then
      echo "build-livecd: missing Plant runtime library: $payload_dir/lib/$library" >&2
      exit 1
    fi
    # Resolve system DSOs beside ld.so, never through a stale JDK RPATH copy.
    rm -f "$openjdk_payload_dir/java/lib/$library"
  done
  openjdk_interp=$work_dir/openjdk.interp
  printf '/lib/ld.so\0' >"$openjdk_interp"
  for launcher in "$openjdk_payload_dir"/java/bin/*; do
    if [ -f "$launcher" ] &&
       ! objcopy --update-section ".interp=$openjdk_interp" "$launcher" \
           >/dev/null 2>&1; then
      echo "build-livecd: unable to patch OpenJDK launcher: $launcher" >&2
      exit 1
    fi
  done
fi

if [ "$architecture" = i386 ]; then
  mkdir -p "$payload_dir/tcc/crt" "$payload_dir/tcc/include" \
           "$payload_dir/tcc/inst" "$payload_dir/tcc/lib"
  cp -R "$apps_include_dir"/. "$payload_dir/tcc/include/"
  while IFS= read -r library; do
    cp "$apps_lib_dir/$library" "$payload_dir/tcc/lib/"
  done < "$apps_out_dir/sdk-libraries.list"
  mv "$payload_dir/tcc/lib/libtcc1.a" "$payload_dir/tcc/inst/"
  cp "$apps_out_dir/crti.obj" "$payload_dir/tcc/crt/crti.o"
  cp "$apps_dir/tcc/tcc/crti.c" "$payload_dir/"
fi

payload_kib=$(du -sk "$payload_dir" | awk '{print $1}')
image_mib=$(((payload_kib + payload_kib / 4 + 2048 + 1023) / 1024))
if [ "$image_mib" -lt 16 ]; then
  image_mib=16
fi
image_sectors=$((image_mib * 2048))
truncate -s 0 "$initramfs"
truncate -s "${image_mib}M" "$initramfs"
if [ "$image_mib" -ge 256 ]; then
  mformat -F -T "$image_sectors" -h 64 -s 16 -i "$initramfs"
else
  mformat -T "$image_sectors" -h 64 -s 16 -i "$initramfs"
fi
mcopy -s -i "$initramfs" "$payload_dir"/* ::/

if [ -n "$openjdk_disk" ]; then
  openjdk_payload_kib=$(du -sk "$openjdk_payload_dir" | awk '{print $1}')
  openjdk_image_mib=$(((openjdk_payload_kib + openjdk_payload_kib / 4 + 2048 + 1023) / 1024))
  if [ "$openjdk_image_mib" -lt 16 ]; then
    openjdk_image_mib=16
  fi
  openjdk_image_sectors=$((openjdk_image_mib * 2048))
  mkdir -p "$(dirname "$openjdk_disk")"
  truncate -s 0 "$openjdk_disk"
  truncate -s "${openjdk_image_mib}M" "$openjdk_disk"
  if [ "$openjdk_image_mib" -ge 256 ]; then
    mformat -F -T "$openjdk_image_sectors" -h 64 -s 16 -i "$openjdk_disk"
  else
    mformat -T "$openjdk_image_sectors" -h 64 -s 16 -i "$openjdk_disk"
  fi
  mcopy -s -i "$openjdk_disk" "$openjdk_payload_dir"/* ::/
fi

# Verify applications through their original names, including VFAT long names.
while IFS= read -r program; do
  path=$program
  if [ "$program" = doom.bin ]; then path=games/$program; fi
  printf '/%s\n' "$path"
done < "$apps_out_dir/applications.list" > "$payload_dir/apps.lst"
mcopy -i "$initramfs" "$payload_dir/apps.lst" ::/apps.lst

if [ "$architecture" = i386 ]; then
setup_manifest=$payload_dir/setup.mst
loader_source=$(fat_short_path "$initramfs" DOSLDR.bin)
{
  printf '"files" = [\n'
  printf '    {"type" = "file" "source" = "%s" "path" = "DOSLDR.bin"}' \
    "$loader_source"
  find "$payload_dir" -mindepth 1 -type d -printf '%P\n' | sort |
    while IFS= read -r path; do
      printf ',\n    {"type" = "dir" "path" = "%s"}' "$path"
    done
  find "$payload_dir" -type f ! -name DOSLDR.bin ! -name setup.mst \
      -printf '%P\n' | sort |
    while IFS= read -r path; do
      source=$(fat_short_path "$initramfs" "$path")
      printf ',\n    {"type" = "file" "source" = "%s" "path" = "%s"}' \
        "$source" "$path"
    done
  printf ',\n    {"type" = "file" "source" = "SETUP.MST" "path" = "setup.mst"}\n]\n'
} >"$setup_manifest"
mcopy -i "$initramfs" "$setup_manifest" ::/setup.mst

fi

gzip -n -6 -c "$initramfs" > "$iso_root/boot/initramfs.img.gz"

cp "$object_dir/kernel.bin" "$iso_root/boot/kernel.bin"
if [ "$architecture" = x86_64 ]; then
  cp "$kernel_dir/res/limine-x86_64.conf" "$iso_root/limine.conf"
  cp "$limine_dir/limine-uefi-cd.bin" "$iso_root/boot/limine/"
  mkdir -p "$iso_root/EFI/BOOT"
  cp "$limine_dir/BOOTX64.EFI" "$iso_root/EFI/BOOT/"
else
  cp "$kernel_dir/res/limine.conf" "$iso_root/limine.conf"
fi
cp "$limine_dir/limine-bios-cd.bin" "$iso_root/boot/limine/"
cp "$limine_dir/limine-bios.sys" "$iso_root/boot/limine/"
cp "$limine_dir/LICENSE" "$iso_root/boot/limine/"

set --
if [ "$architecture" = x86_64 ]; then
  if [ "$iso_maker" != xorriso ]; then
    echo "build-livecd: x86_64 hybrid UEFI images require xorriso" >&2
    exit 1
  fi
  set -- --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part \
    --efi-boot-image --protective-msdos-label
fi
if [ "$iso_maker" = xorriso ]; then
  xorriso -as mkisofs \
    -R -J -joliet-long -iso-level 3 -V PLANT_OS_LIVE \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o "$output" "$@" "$iso_root"
else
  genisoimage \
    -R -J -joliet-long -iso-level 3 -V PLANT_OS_LIVE \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o "$output" "$@" "$iso_root"
fi
"$limine_dir/limine" bios-install --force "$output"

echo "Plant OS LiveCD: $output"
if [ -n "$openjdk_disk" ]; then
  echo "Plant OS OpenJDK disk: $openjdk_disk"
fi
