# See LICENSE file for copyright and license details.

PREFIX = /usr/local

UNICODE = 9.0.0

CFLAGS  = -m32 -I../include -nostdinc -nostdlib -fno-builtin -ffreestanding -fno-stack-protector -Qn -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -march=pentium -O0 -w -c
LDFLAGS = -s

CC  = gcc
AWK = awk
