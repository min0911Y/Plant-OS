#!/usr/bin/env python3
"""Build OpenAL Soft's C mixer and null/loopback backends for Plant x86_64."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
from sources import ROOT, Sources, digest, publish


def build(output, jobs):
    port = ROOT / 'apps/openal'
    source = Sources(port)['openal'].prepare()
    runtime = ROOT / 'apps/out/x86_64'
    output.mkdir(parents=True, exist_ok=True)
    library = runtime / 'lib/libopenal.so'
    state = json.dumps({
        'source': digest(source / '.plant-source-sha256'),
        'config': digest(port / 'config.h'), 'script': digest(Path(__file__)),
        'runtime': digest(runtime / 'lib/libp.so'),
    }, sort_keys=True) + '\n'
    stamp = output / '.plant-build'
    if stamp.exists() and stamp.read_text() == state and library.exists():
        return
    publish(output / 'version.h', '#define ALSOFT_VERSION "1.19.1"\n'
            '#define ALSOFT_GIT_BRANCH "PlantOS"\n#define ALSOFT_GIT_COMMIT_HASH "release"\n')
    generator = output / 'bsincgen'
    subprocess.run(['cc', '-O2', str(source / 'native-tools/bsincgen.c'), '-lm', '-o', str(generator)], check=True)
    subprocess.run([str(generator), str(output / 'bsinc_inc.h')], check=True)
    includes = [ROOT / 'apps/include', port, output, source / 'include',
                source / 'OpenAL32/Include', source / 'Alc', source / 'common']
    gcc_include = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    flags = ['gcc', '-std=gnu11', '-O2', '-m64', '-mno-red-zone', '-mno-mmx', '-msse2',
             '-mfpmath=sse', '-mlong-double-64', '-fPIC', '-ffreestanding', '-fno-builtin',
             '-fno-stack-protector', '-fvisibility=hidden', '-nostdinc', '-isystem', gcc_include,
             '-DPLANT_ARCH_X86_64', '-D__plantos__', '-DAL_BUILD_LIBRARY', '-DAL_ALEXT_PROTOTYPES',
             '-DNDEBUG', '-U__linux__', '-U__linux', '-Ulinux']
    for directory in includes:
        flags += ['-I', str(directory)]
    sources = sorted(path for directory in ['common', 'OpenAL32', 'Alc', 'Alc/effects', 'Alc/filters']
                     for path in (source / directory).glob('*.c'))
    sources += [source / ('Alc/' + name + '.c') for name in
                ['backends/base', 'backends/null', 'backends/loopback',
                 'mixer/mixer_c', 'mixer/mixer_sse', 'mixer/mixer_sse2']]

    def compile(path):
        obj = output / 'objects' / path.relative_to(source).with_suffix('.o')
        obj.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([*flags, '-c', str(path), '-o', str(obj)], check=True)
        return obj

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        objects = list(pool.map(compile, sources))
    subprocess.run(['ld', '-m', 'elf_x86_64', '-z', 'max-page-size=4096', '-z', 'noexecstack',
                    '-z', 'relro', '-z', 'now', '-z', 'text', '-shared', '--no-undefined',
                    '--hash-style=both', '-soname', 'libopenal.so', '-o', str(library),
                    *map(str, objects), str(runtime / 'dynamic/libp/dso.o'), str(runtime / 'lib/libp.so')], check=True)
    publish(stamp, state)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    build(args.output.resolve(), args.jobs)
