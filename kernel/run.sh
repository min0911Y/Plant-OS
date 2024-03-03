#!/bin/bash 
qemu-system-x86_64 -net nic,model=pcnet -net user -serial stdio -device sb16 -device floppy -s -S -fda ./img/Powerint_DOS_386.img -boot a -m 256
