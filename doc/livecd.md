# Plant OS LiveCD

LiveCD 使用 Limine 加载 `kernel.bin`：i386 使用 Multiboot2，x86_64 使用 Limine native
协议。两种架构均把 FAT 镜像压缩为 `/boot/initramfs.img.gz`，配置通过
`module_path: $boot():/boot/initramfs.img.gz` 让 Limine 透明解压，再作为 initramfs
module 交给内核。内核在分页与堆初始化前保留该物理内存，随后把它
注册为 `R:` 启动盘；运行期间的写入只保存在内存中，重启后丢失。

先按仓库标准顺序完成构建，再生成 ISO：

```sh
make -C apps
make -C loader
make -C kernel livecd
```

首次执行会下载并校验固定版本的 Limine 12.6.1。LiveCD 额外依赖 `curl`、`tar`、`gzip`、
`xorriso`（也可使用 `genisoimage`）和现有的 mtools，产物为
`kernel/plant-os-livecd.iso`。

```sh
make -C kernel livecd_run
```

Limine 菜单的默认项直接启动 `kernel.bin + initramfs`。第二项会 chainload 第一块硬盘，
用于启动仍由 `Mimg` 生成的 `boot.img` 和现有 DOSLDR 链；LiveCD 默认启动路径不经过
DOSLDR。原有 `make -C kernel img_run` 也保持不变。

initramfs 按对应架构构建图生成的 `applications.list` 收录应用；应用注册后自动进入镜像。除 `doom.bin` 与配套的 `doom1.wad` 放在 `/games` 外，应用默认位于根目录。
Lite 的完整 `apps/lite-1.11/data` 运行时资源复制到 `/data`，包括 core、字体、插件和
用户配置。
`boot.bin`、`boot32.bin`、`boot_pfs.bin` 与 `DOSLDR.bin` 保留在 initramfs 中，作为
FAT/PFS 格式化所需的引导模板和加载器输入；Limine 默认启动仍只加载 `kernel.bin` 与
initramfs，不执行其中的 DOSLDR。
TCC 开发文件沿用 `tcc.img` 的绝对路径布局：完整的 `apps/include` 位于
`/tcc/include`，静态库位于 `/tcc/lib`，`libtcc1.a` 位于 `/tcc/inst`，根目录保留
供 `tccinst.bin` 使用的 `crti.c`；TCC 构建生成的 `crti.o` 位于 `/tcc/crt`，因此
LiveCD 上的编译器可以直接链接应用。SDK 归档由 `apps/build.mk` 的 `sdk` 目标生成，
打包脚本按 `sdk-libraries.list` 复制到镜像。

LiveCD 构建在 FAT initramfs 完成后自动生成 `/setup.mst`。清单覆盖 initramfs 中的全部
目录和文件，`DOSLDR.bin` 固定为第一个安装文件；`source` 记录源盘的实际 FAT
短别名，`path` 记录安装目标原名。`setup1.bin` 从自身所在盘复制到 C:，FAT 与 PFS
均保留原始名称；FAT 通过 VFAT LFN 创建长名及唯一短别名，不再使用第二份短名目标路径。
`apps.lst` 也使用应用构建清单中的原名。initramfs 已由 mtools 写入标准 VFAT 目录项，
内核与普通 FAT 磁盘共用同一套读写实现，见 [FAT 长文件名](fat-lfn.md)。

i386 LiveCD 使用 legacy BIOS；x86_64 LiveCD 同时支持 BIOS 与 UEFI，构建命令为
`make -C kernel ARCH=x86_64 livecd`，产物为 `kernel/plant-os-x86_64.iso`。

FAT 内容与安装清单全部写入后，打包脚本使用 `gzip -n -6` 压缩，省略 gzip 中的
原文件名和时间戳。未压缩镜像保留在对应对象目录的 `livecd/initramfs.img`，只将
压缩版本放入 ISO。压缩减少 ISO 大小与引导读取量，不减少解压后的 RAM 盘占用。
验证时可用 `gzip -dc` 与未压缩镜像逐字节比较，并分别冷启动 i386 BIOS、
x86_64 BIOS 与 UEFI，检查 `R:` 挂载及用户程序运行。
