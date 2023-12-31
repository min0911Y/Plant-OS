#include <dosldr.h>
#include <ELF.h>
struct TASK MainTask;
void *malloc(int size);
void *memcpy(void *s, const void *ct, size_t n);
bool elf32Validate(Elf32_Ehdr* hdr) {
  return hdr->e_ident[EI_MAG0] == ELFMAG0 && hdr->e_ident[EI_MAG1] == ELFMAG1 &&
         hdr->e_ident[EI_MAG2] == ELFMAG2 && hdr->e_ident[EI_MAG3] == ELFMAG3;
}
void load_segment(Elf32_Phdr* phdr, void* elf) {
  printf("%08x %08x %d\n",phdr->p_vaddr,phdr->p_offset,phdr->p_filesz);
  memcpy(phdr->p_vaddr, elf + phdr->p_offset, phdr->p_filesz);
  if (phdr->p_memsz > phdr->p_filesz) {  // 这个是bss段
    memset(phdr->p_vaddr + phdr->p_filesz, 0,
           phdr->p_memsz - phdr->p_filesz);
  }
}
uint32_t load_elf(Elf32_Ehdr* hdr) {
  Elf32_Phdr* phdr = (Elf32_Phdr*)((uint32_t)hdr + hdr->e_phoff);
  for (int i = 0; i < hdr->e_phnum; i++) {
    load_segment(phdr,(void *)hdr);
    phdr++;
  }
  return hdr->e_entry;
}

void DOSLDR_MAIN() {
  struct MEMMAN *memman = MEMMAN_ADDR;
  unsigned int memtotal;
  memtotal = 128*1024*1024;
  memman_init(memman);
  memman_free(memman, 0x00600000, memtotal - 0x00600000);
  //asm("mov $0x00650000,%esp");
  clear();
  init_gdtidt();
  init_pic();
  io_sti();    /* IDT/PIC的初始化已经完成，于是开放CPU的中断 */
  init_vdisk();
  init_vfs(); 
  init_floppy();
  Register_fat_fileSys();
  reg_pfs();
  ide_initialize(0x1F0, 0x3F6, 0x170, 0x376, 0x000);
  char default_drive;
  unsigned int default_drive_number;
  if (memcmp((void *)"FAT12   ", (void *)0x7c00 + BS_FileSysType, 8) == 0 ||
      memcmp((void *)"FAT16   ", (void *)0x7c00 + BS_FileSysType, 8) == 0) {
    if (*(unsigned char *)(0x7c00 + BS_DrvNum) >= 0x80) {
      default_drive_number =
          *(unsigned char *)(0x7c00 + BS_DrvNum) - 0x80 + 0x02;
    } else {
      default_drive_number = *(unsigned char *)(0x7c00 + BS_DrvNum);
    }
  } else if (memcmp((void *)"FAT32   ",
                    (void *)0x7c00 + BPB_Fat32ExtByts + BS_FileSysType,
                    8) == 0) {
    if (*(unsigned char *)(0x7c00 + BPB_Fat32ExtByts + BS_DrvNum) >= 0x80) {
      default_drive_number =
          *(unsigned char *)(0x7c00 + BPB_Fat32ExtByts + BS_DrvNum) - 0x80 +
          0x02;
    } else {
      default_drive_number = *(unsigned char *)(0x7c00 + BPB_Fat32ExtByts + BS_DrvNum);
    }
  } else {
    if (*(unsigned char *)(0x7c00) >= 0x80) {
      default_drive_number =
          *(unsigned char *)(0x7c00) - 0x80 + 0x02;
    } else {
      default_drive_number = *(unsigned char *)(0x7c00);
    }
  }
  default_drive = default_drive_number + 0x41;
  NowTask()->drive = default_drive;
  NowTask()->drive_number = default_drive_number;
  vfs_mount_disk(NowTask()->drive, NowTask()->drive);
  vfs_change_disk(NowTask()->drive);
  printf("DOSLDR 386 v0.2\n");
  printf("Copyright zhouzhihao & min0911 2022\n");
  printf("memtotal=%dMB\n", memtotal / 1024 / 1024);
  char path[15] = " :\\kernel.bin";
  path[0] = NowTask()->drive;
  printf("Load file:%s\n", path);
  int sz = vfs_filesize(path);
  if(sz == -1) {
    printf("DOSLDR can't find kernel.bin in Drive %c",path[0]);
    for(;;);
  }
  // printf("fp = %08x\n%d\n",fp, fp->size);
  unsigned char *s = page_malloc(sz);
  printf("Will load in %08x size = %08x\n", s, sz);
  vfs_readfile(path, s);
  printf("Loading...\n");
  uint32_t entry = load_elf((Elf32_Ehdr *)s);

  //printf("ESP:%08x\n",*(unsigned int *)(0x00280000 + 12));
  _IN(2*8,entry);
}
struct TASK *NowTask() {
  return &MainTask;
}
