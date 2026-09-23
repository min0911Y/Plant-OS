#!/usr/bin/env python3
"""Package a Minecraft 1.20.1 client archive with the native Plant JDK/LWJGL."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import zipfile

from sources import ROOT, Sources

LWJGL_BUILD = importlib.import_module('build-lwjgl')


def prepare(archive, payload, lwjgl, game_dir='C:/java/mc', lwjgl_dir='C:/java/lwjgl'):
    prefix = '.minecraft/'
    version = json.loads(archive.read(prefix + 'versions/1.20.1/1.20.1.json'))
    if version['id'] != '1.20.1' or version['mainClass'] != 'net.minecraft.client.main.Main':
        raise ValueError('expected the vanilla Minecraft 1.20.1 client')

    def extract(member, relative, checksum):
        relative = Path(relative)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'invalid archive destination: {relative}')
        data = archive.read(prefix + member)
        if hashlib.sha1(data).hexdigest() != checksum:
            raise ValueError(f'SHA-1 mismatch: {member}')
        target = payload / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)

    extract('versions/1.20.1/1.20.1.jar', 'client.jar', version['downloads']['client']['sha1'])
    classpath = [game_dir + '/client.jar']
    classes = payload / 'classes'
    classes.mkdir()
    subprocess.run([LWJGL_BUILD.javac_tool(), '--release', '17',
                    '-d', str(classes), str(ROOT / 'apps/minecraft/ClientLauncher.java')], check=True)
    classpath.append(game_dir + '/classes')
    for library in version['libraries']:
        allowed = not library.get('rules')
        for rule in library.get('rules', []):
            if rule.get('os', {}).get('name', 'plantos') == 'plantos':
                allowed = rule['action'] == 'allow'
        if not allowed:
            continue
        artifact = library.get('downloads', {}).get('artifact')
        if not artifact or 'natives-' in library['name']:
            continue
        name = library['name'].split(':')[1]
        if name == 'lwjgl-stb' and library['name'] != 'org.lwjgl:lwjgl-stb:3.3.1':
            raise ValueError('client STB JAR must match the pinned 3.3.1 native source')
        if name != 'lwjgl-stb' and library['name'].startswith('org.lwjgl:') and (lwjgl / 'jar' / f'{name}-3.3.6.jar').exists():
            continue
        relative = Path('libraries') / Path(artifact['path']).name
        extract('libraries/' + artifact['path'], relative, artifact['sha1'])
        classpath.append(game_dir + '/' + relative.as_posix())
    classpath.extend(lwjgl_dir + '/jar/' + path.name for path in sorted((lwjgl / 'jar').glob('*.jar'))
                     if not path.name.startswith('lwjgl-stb-'))
    index = version['assetIndex']
    extract('assets/indexes/' + index['id'] + '.json', 'assets/indexes/' + index['id'] + '.json', index['sha1'])
    assets = json.loads((payload / 'assets/indexes' / (index['id'] + '.json')).read_text())
    for checksum in sorted({asset['hash'] for asset in assets['objects'].values()}):
        relative = 'assets/objects/' + checksum[:2] + '/' + checksum
        extract(relative, relative, checksum)
    (payload / 'options.txt').write_text(
        'version:3465\nrenderDistance:2\nsimulationDistance:5\nmaxFps:30\n'
        'enableVsync:false\nfullscreen:false\nrawMouseInput:false\nguiScale:2\n'
        'graphicsMode:0\nclouds:false\nparticles:2\nmipmapLevels:0\n'
        'autoJump:false\nonboardAccessibility:false\n')
    arguments = [
        '-Xms256m', '-Xmx1536m', '-XX:ErrorFile=' + game_dir + '/hs_err.log',
        '-Dorg.lwjgl.librarypath=' + game_dir + '/native;' + lwjgl_dir + '/native',
        '-Djava.library.path=' + game_dir + '/native;' + lwjgl_dir + '/native',
        '-Dorg.lwjgl.glfw.libname=libglfw.so',
        '-Dorg.lwjgl.opengl.libname=libGL.so',
        '-Dorg.lwjgl.system.allocator=system',
        '-cp', ';'.join(classpath), 'org.plantos.launcher.ClientLauncher', version['mainClass'],
        '--username', 'PlantPlayer', '--version', '1.20.1',
        '--gameDir', game_dir, '--assetsDir', game_dir + '/assets',
        '--assetIndex', index['id'], '--uuid', '4b9e6e842f823a7b8aefbd09e529a1f6',
        '--accessToken', '0', '--userType', 'legacy', '--versionType', 'release',
        '--width', '640', '--height', '480',
    ]
    (payload / 'client.args').write_text('\n'.join(arguments) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--jdk', type=Path, default=ROOT / 'apps/out/x86_64/openjdk/images/jdk')
    parser.add_argument('--lwjgl', type=Path, default=ROOT / 'apps/out/x86_64/lwjgl')
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--disk', type=Path, required=True)
    parser.add_argument('--disk-mib', type=int, default=3072)
    args = parser.parse_args()
    if args.disk.exists():
        parser.error(f'disk already exists: {args.disk}; choose a new output to preserve worlds')
    if args.disk_mib < 2048:
        parser.error('client disk needs at least 2048 MiB for the JDK, assets and worlds')
    if not (args.jdk / 'lib/server/libjvm.so').is_file():
        parser.error('build the Plant Server JDK before packaging the client')
    args.iso.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.disk.resolve().parent.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'apps/out/x86_64/minecraft-client'
    work.mkdir(parents=True, exist_ok=True)
    source = Sources(ROOT / 'apps/minecraft')['stb'].prepare()
    stb = LWJGL_BUILD.build_stb_native(source, work / 'stb')
    with tempfile.TemporaryDirectory(prefix='payload-', dir=work) as temporary:
        payload = Path(temporary) / 'mc'
        payload.mkdir()
        (payload / 'native').mkdir()
        shutil.copyfile(stb, payload / 'native/liblwjgl_stb.so')
        with zipfile.ZipFile(args.archive) as archive:
            prepare(archive, payload, args.lwjgl)
        (payload / 'run-client.lua').write_text(
            'assert(os.execute("lwjgl-launcher.bin --directory C:/java/mc '
            'C:/java/bin/java @C:/java/mc/client.args"))\n')
        init = Path(temporary) / 'init.mst'
        init.write_text('"todo" = [\n{"action" = "run" "command_line" = "lua.bin C:/java/mc/run-client.lua"},\n'
                        '{"action" = "run" "command_line" = "psh.bin"}\n]\n')
        environment = dict(os.environ, PLANT_OPENJDK_DIR=str(args.jdk.resolve()),
                           PLANT_OPENJDK_DISK=str(args.disk.resolve()),
                           PLANT_OPENJDK_DISK_MIN_MIB=str(args.disk_mib),
                           PLANT_LWJGL_DIR=str(args.lwjgl.resolve()),
                           PLANT_MINECRAFT_DIR=str(payload), PLANT_INIT_SCRIPT=str(init))
        subprocess.run([str(ROOT / 'scripts/build-livecd.sh'), str(args.iso.resolve()), 'x86_64'],
                       env=environment, check=True)


if __name__ == '__main__':
    main()
