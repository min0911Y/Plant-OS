#!/usr/bin/bash

OS_NAME=$(lsb_release -si)

pkg_manager_update=""

case $OS_NAME in
Ubuntu | Debian)
  pkg_manager_update="apt-get update"
  qemu_packages="apt-get install qemu-system-x86 qemu-system-gui"
  mtools_packages="apt-get install mtools"
  ;;
Fedora)
  pkg_manager_update="dnf makecache"
  qemu_packages="dnf install qemu-kvm qemu-kvm-tools"
  mtools_packages="dnf install mtools"
  ;;
CentOS)
  pkg_manager_update="yum makecache"
  qemu_packages="yum install qemu-kvm qemu-kvm-tools"
  mtools_packages="yum install mtools"
  ;;
Arch)
  pkg_manager_update="pacman -Syy"
  qemu_packages="pacman -S qemu"
  mtools_packages="pacman -S mtools"
  ;;
openSUSE)
  pkg_manager_update="zypper refresh"
  qemu_packages="zypper install qemu qemu-kvm-spice"
  mtools_packages="zypper install mtools"
  ;;
esac

if [ -n "$pkg_manager_update" ]; then
  if ! command -v qemu-system-x86_64 &>/dev/null; then
    suto $qemu_packages
  fi

  if ! command -v mtools &>/dev/null; then
    suto $mtools_packages
  fi
fi

cd ./apps && make -j
if [ $? -ne 0 ]; then
  echo "在 apps 目录中构建失败"
  exit
fi

cd ../Loader && make -j
if [ $? -ne 0 ]; then
  echo "在 Loader 目录中构建失败"
  exit
fi

cd ../kernel && make -j
if [ $? -ne 0 ]; then
  echo "在 kernel 目录中构建失败"
  exit
fi

exho "构建成功，准备运行"
make run
