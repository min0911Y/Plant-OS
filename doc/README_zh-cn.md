
[English](../README.md) \| 中文

# 关于 Plant OS

- Plant OS 是一个仅用于学习目的的操作系统。
- 最初，操作系统是 16 位实模式，但现在是 32 位保护模式（386 版本）。
- 由于 COVID-19，Zhou Zhihao待在家里，并于 2020 年 12 月启动了该项目。
- 该项目是关于操作系统的，Zhou Zhihao将其命名为“Powerint”，意思是我们可以使用的强大中断。经过大约一年的编码，操作系统具有与 MS-DOS 类似的正常功能，但它仍处于 16 位实模式。
- 2021 年 12 月，Simple OS 的作者Qiu Chenjun与Zhou Zhihao合作。他们帮助 Plant OS 过渡到一个新世界，32 位保护模式，并将其更名为 Plant OS。
- 经过一年多的编码，Plant OS 在不断改进。
**无论如何，你应该知道 Plant OS 是为学习计算机工作原理而制作的，它不能成为你日常工作的操作系统。而且 Plant OS 仍有许多错误，如果您愿意并且能够，您可以修复这些错误并发出拉取请求，我们会合并。顺便说一句，操作系统可能永远处于保护模式，因为我们仍然是学生，我们没有足够的时间来改进操作系统，请原谅我们。另外，如果你发现一些错误，你可以提出问题，我们会尽快修复它（如果我们有足够的能力修复）**

## 构建

**注意：在构建之前，你可能需要安装 nasm、gcc、g++、mtools 和 qemu**

首先，你必须克隆 repo，如下所示：

```cmd
git clone https://github.com/min0911Y/Plant-OS.git
```

其次，转到 apps 文件夹：

```cmd
cd apps
```

然后，使用 `make` 编译应用程序：

```cmd
make
```

如果你没有看到错误消息，则转到 `Loader` 文件夹，然后在 cmd 提示符中输入 `make`：

```cmd
cd ..
cd Loader
make
```

如果你没有看到错误消息，则您可以运行以下命令进入 `kernel` 文件夹并构建内核：

```cmd
cd ..
cd kernel
make
```

或者您可以添加 `run` 以便在编译后启动调试：

```cmd
make run
```

您将看到 Powerint DOS 在 kernel/img 文件夹中分成四个图像。

**完成！您现在可以使用 qemu 或任何其他您喜欢的虚拟化软件尝试 Powerint DOS！**

## 启动

在 `kernel` 目录中：

```cmd
make full_run
```

您还可以使用 `make run` 或 `make img_run`，它们有所不同。

## Doom 游戏

如果您想运行 Doom，在构建后：

1. 您可以二进制连接 `kernel/img/doom1.img` 和 `kernel/img/doom2.img`。之后，在 `kernel` 目录下运行：

```cmd
qemu-system-i386 -net nic,model=pcnet -net user -serial stdio -device sb16 -device floppy -fda ./img/Powerint_DOS_386.img -drive id=disk,file=disk.img,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -hdb <YOUR-DOOM-HARD-DISK-FILE-NAME> -boot a -m 512 -enable-kvm
```

2. 你也可以使用 PlantOS 提供的 `doomcpy`，参见 [doomcpy.c](apps/doomcpy/doomcpy.c)。

## 开发者

- zhouzhihao <https://github.com/ZhouZhihaos>

- min0911_ <https://github.com/min0911Y>

## 谢谢

- TheFlySong
- yywd_123
- Oildum-was-ejected
- wenxuanjun
- duoduo70(time.c)
- ...

## 关于问题

很高兴看到您想通过问题报告错误。但无论如何，您应该遵循一些规则，以帮助我们快速修复错误。

这就是 [规则](issue_rules.md)

如果您同意遵守规则，请随时发送问题，无论问题有多严重。
