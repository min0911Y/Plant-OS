#!/usr/bin/env python3
"""Prepare a relocatable Plant OS Minecraft directory for a FAT32 USB drive."""
import argparse
import hashlib
import importlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from sources import ROOT, Sources

CLIENT = importlib.import_module('build-minecraft-client')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--system-image', type=Path,
                        default=ROOT / 'kernel/plant-os-x86_64.iso')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error('output already exists; choose a new directory to preserve saves')
    runtime = ROOT / 'apps/out/x86_64'
    jdk = runtime / 'openjdk/images/jdk'
    lwjgl = runtime / 'lwjgl'
    if not (jdk / 'lib/server/libjvm.so').is_file() or not args.system_image.is_file():
        parser.error('build the Plant Server JDK and x86_64 LiveCD first')
    source = Sources(ROOT / 'apps/minecraft')['stb'].prepare()
    stb = CLIENT.LWJGL_BUILD.build_stb_native(source, runtime / 'minecraft-client/stb')
    shutil.copytree(jdk, output / 'java')
    # Match LiveCD deployment: upstream launchers still carry the host interp
    # string; system DSOs must resolve from the boot image, not stale JDK copies.
    for name in ('libp.so', 'libcpp.so', 'libm.so.6', 'libz.so.1'):
        (output / 'java/lib' / name).unlink(missing_ok=True)
    with tempfile.NamedTemporaryFile() as interpreter:
        interpreter.write(b'/lib/ld.so\0')
        interpreter.flush()
        for launcher in sorted((output / 'java/bin').iterdir()):
            if launcher.is_file():
                subprocess.run(['objcopy', '--update-section',
                                '.interp=' + interpreter.name, str(launcher)], check=True)
    for directory in ('jar', 'native'):
        shutil.copytree(lwjgl / directory, output / 'java/lwjgl' / directory)
    game = output / 'java/mc'
    (game / 'native').mkdir(parents=True)
    shutil.copyfile(stb, game / 'native/liblwjgl_stb.so')
    with zipfile.ZipFile(args.archive) as archive:
        CLIENT.prepare(archive, game, lwjgl, game_dir='.', lwjgl_dir='../lwjgl')
    shutil.copyfile(runtime / 'lwjgl-launcher.bin', output / 'lwjgl-launcher.bin')
    shutil.copyfile(ROOT / 'apps/minecraft/run-usb.lua', output / 'run.lua')
    (output / 'system').mkdir()
    shutil.copyfile(args.system_image, output / 'system/plant-os-x86_64.iso')
    (output / 'README.txt').write_text(
        'Plant OS 原版 Minecraft Java 1.20.1 U 盘文件包\n\n'
        '1. 使用最新 x86_64 Plant OS。配套启动镜像在 system/plant-os-x86_64.iso；'
        '它提供系统的 Mesa/GLFW/OpenAL/libp 运行库。i386 和旧系统不可用。\n'
        '2. 将整个 plmc 文件夹复制到 FAT32 U 盘根目录。支持无分区或 MBR 主分区；'
        '不支持 GPT、exFAT、NTFS。复制文件本身不会把 U 盘做成启动盘。\n'
        '3. 启动 Plant OS，插入 U 盘，在默认终端运行 disks.bin 查看挂载盘符。\n'
        '4. 假设 U 盘挂载为 E:/，执行：\n\n'
        '   lua.bin E:/plmc/run.lua\n\n'
        '将 E: 换成实际盘符；无需 cd，不需要改配置。文件夹也可改名，命令相应修改。\n'
        '游戏、Java 和资源均从该文件夹读取。至少 4 GiB 内存，U 盘建议留出 2 GiB '
        '以上空间及额外存档空间。首次启动和生成世界请耐心等待。\n\n'
        '存档：java/mc/saves/\n日志：java/mc/client.log、java/mc/logs/latest.log\n'
        '配置：java/mc/options.txt；JVM 参数：java/mc/client.args\n'
        '默认离线用户 PlantPlayer，640x480、2 区块视距、30 FPS 上限；'
        '软件渲染、无扬声器输出。未附带账户凭据或测试存档。\n'
        '先在游戏中保存退出，再退出客户端，等写入完成后拔盘。\n'
        'SHA256SUMS.txt 记录交付文件校验值；运行后配置和日志会变化。\n', encoding='utf-8')
    with (output / 'SHA256SUMS.txt').open('w') as manifest:
        for path in sorted(output.rglob('*')):
            if path.is_file() and path.name != 'SHA256SUMS.txt':
                with path.open('rb') as stream:
                    checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
                manifest.write(checksum + '  ' + path.relative_to(output).as_posix() + '\n')
    print(output)


if __name__ == '__main__':
    main()
