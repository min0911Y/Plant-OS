# Plant OS LiveCD

LiveCD 使用 Limine 直接按 Multiboot2 加载 32 位 `kernel.bin`，并把一个可写的 FAT
镜像作为 initramfs module 交给内核。内核在分页与堆初始化前保留该物理内存，随后把它
注册为 `R:` 启动盘；运行期间的写入只保存在内存中，重启后丢失。

先按仓库标准顺序完成构建，再生成 ISO：

```sh
make -C apps
make -C loader
make -C kernel livecd
```

首次执行会下载并校验固定版本的 Limine 12.6.1。LiveCD 额外依赖 `curl`、`tar`、
`xorriso`（也可使用 `genisoimage`）和现有的 mtools，产物为
`kernel/plant-os-livecd.iso`。

```sh
make -C kernel livecd_run
```

Limine 菜单的默认项直接启动 `kernel.bin + initramfs`。第二项会 chainload 第一块硬盘，
用于启动仍由 `Mimg` 生成的 `boot.img` 和现有 DOSLDR 链；LiveCD 默认启动路径不经过
DOSLDR。原有 `make -C kernel img_run` 也保持不变。

initramfs 会动态收录 `apps/out/*.bin` 中的全部应用，新产物无需再逐项加入内核
Makefile。除 `doom.bin` 与配套的 `doom1.wad` 放在 `/games` 外，应用默认位于根目录。
Lite 的完整 `apps/lite-1.11/data` 运行时资源复制到 `/data`，包括 core、字体、插件和
用户配置。
`boot.bin`、`boot32.bin`、`boot_pfs.bin` 与 `DOSLDR.bin` 保留在 initramfs 中，作为
FAT/PFS 格式化所需的引导模板和加载器输入；Limine 默认启动仍只加载 `kernel.bin` 与
initramfs，不执行其中的 DOSLDR。
TCC 开发文件沿用 `tcc.img` 的绝对路径布局：完整的 `apps/include` 位于
`/tcc/include`，静态库位于 `/tcc/lib`，`libtcc1.a` 位于 `/tcc/inst`，根目录保留
供 `tccinst.bin` 使用的 `crti.c`；TCC 构建生成的 `crti.o` 位于 `/tcc/crt`，因此
LiveCD 上的编译器可以直接链接应用。

LiveCD 构建在 FAT initramfs 完成后自动生成 `/setup.mst`。清单覆盖 initramfs 中的全部
目录和文件，`DOSLDR.bin` 固定为第一个安装文件，并同时记录源盘的实际 FAT 短别名、
PFS 目标原名和 FAT 目标 8.3 名。`setup1.bin` 不再假定安装源是 A:，而是从自身所在盘
复制到 C:；用户仍可选择 FAT 或 PFS。选择 FAT 时，超过 8.3 的名称使用 mtools 为
LiveCD 生成的无冲突短别名，选择 PFS 时保留原始名称。

当前内核仍依赖 BIOS 实模式服务，因此 LiveCD 只生成 legacy BIOS 启动入口，不宣称
支持 UEFI 启动。
