
中文

# Plant OS 史诗级大更新

<span style="color:cyan">***PLOS 使用 CMake 啦***</span>

## TODO （现在是 幻想时间！）

- [ ] 使用 CMake 构建系统
  - [x] loader
  - [x] kernel
  - [x] libtcc1
  - [x] fattools
  - [x] netgobang
  - [ ] apps
- [ ] 支持在 vscode 中直接使用 gdb 调试
- [ ] 支持使用 Clang 编译
  - [x] loader (可编译，但无法正常运行)
  - [ ] kernel
  - [ ] libtcc1
  - [ ] fattools
  - [ ] netgobang
  - [ ] apps
- [ ] 重构 shell (参考 bash)
- [ ] [stamon](https://github.com/CLimber-Rong/stamon) <-- 去催更
- [ ] 支持真机启动
- [ ] eHCI or xHCI 控制器支持
- [ ] 类 linux 的系统调用 (兼容性)
- [ ] 声卡API
- [ ] C++ ABI
- [ ] 动态链接器
- [ ] 现代化 GUI (丢给 [PLUI](https://github.com/plos-clan/plui))
- [ ] 更快的多任务调度
- [ ] 更快、更安全的分页内存管理
- [ ] 用户系统
  - [ ] 多用户
  - [ ] 登录
  - [ ] 权限管理
- [ ] 文件系统 API 改进
- [ ] 文件系统支持
  - [ ] ntfs
    - [ ] 读
    - [ ] 写
  - [ ] btrfs
    - [ ] 读
    - [ ] 写
  - [ ] ext4
    - [ ] 读
    - [ ] 写
  - [ ] FAT
    - [x] 读
    - [x] 写
    - [ ] 长文件名（LFN）
  - [ ] Shawinfs（自己设计的，还没做完）
    - [ ] 读
    - [ ] 写
  - [ ] CDFS
    - [x] 读
    - [ ] 写
- [ ] 虚拟内存
- [ ] 64 位

---
以下 <span style="color:orange">真·幻想</span>

- [ ] 移植 LLVM
- [ ] 移植 GCC
- [ ] 移植 ffmpeg
- [ ] 移植 QEMU
  - [ ] 玩原神
- [ ] 移植 bochs
- [ ] 移植 chromium
- [ ] 移植 cmake
- [ ] 移植 make
- [ ] 自举
- [ ] 在上面跑 minecraft 服务器
- [ ] 移植 openjdk-jre
- [ ] 移植 部分intel显卡驱动

# 关于 Plant OS

- Plant OS 是一个仅用于学习目的的操作系统。
- 最初，操作系统是 16 位实模式，但现在是 32 位保护模式（386 版本）。
- 由于 COVID-19，Zhou Zhihao待在家里，并于 2020 年 12 月启动了该项目。
- 该项目是关于操作系统的，Zhou Zhihao将其命名为“Powerint”，意思是我们可以使用的强大中断。经过大约一年的编码，操作系统具有与 MS-DOS 类似的正常功能，但它仍处于 16 位实模式。
- 2021 年 12 月，Simple OS 的作者Qiu Chenjun与Zhou hihao合作。他们帮助 Plant OS 过渡到一个新世界，32 位保护模式，并将其更名为 Plant OS。
- 经过一年多的编码，Plant OS 在不断改进。
- 2024 年 7 月，Plant OS 将开始进行大规模重构。
- 仍在重构中，请耐心等待。

**无论如何，你应该知道 Plant OS 是为学习计算机工作原理而制作的，它不能成为你日常工作的操作系统。而且 Plant OS 仍有许多错误，如果您愿意并且能够，您可以修复这些错误并发出拉取请求，我们会合并。顺便说一句，操作系统可能永远处于保护模式，因为我们仍然是学生，我们没有足够的时间来改进操作系统，请原谅我们。另外，如果你发现一些错误，你可以提出问题，我们会尽快修复它（如果我们有足够的能力修复）**

# 编译

在项目根目录中使用 make 命令

其它内容请查看[旧 ReadMe 文档](doc/old-readme/README_zh-cn.md)

## 开发者

- zhouzhihao <https://github.com/ZhouZhihaos>

- min0911_ <https://github.com/min0911Y>

- copi143 <https://github.com/copi143>

## 鸣谢

- TheFlySong
- yywd_123
- Oildum-was-ejected
- wenxuanjun
- duoduo70(time.c)
- ...
