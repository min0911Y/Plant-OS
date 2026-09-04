#ifndef KERNEL_PLATFORM_PC_VBE_H
#define KERNEL_PLATFORM_PC_VBE_H

#include <ctypes.h>

#define VBEINFO_ADDRESS 0x7e00u

typedef struct {
  uint16_t offset;
  uint16_t seg;
} ReadModeFarPointer;

typedef struct {
  uint16_t attributes;
  uint8_t winA, winB;
  uint16_t granularity;
  uint16_t winsize;
  uint16_t segmentA, segmentB;
  uint16_t mode;
  uint16_t reserved2;
  uint16_t bytesPerLine;
  uint16_t width, height;
  uint8_t Wchar, Ychar, planes, bitsPerPixel, banks;
  uint8_t memory_model, bank_size, image_pages;
  uint8_t reserved0;
  uint8_t red_mask, red_position;
  uint8_t green_mask, green_position;
  uint8_t blue_mask, blue_position;
  uint8_t rsv_mask, rsv_position;
  uint8_t directcolor_attributes;
  uint32_t physbase;
  uint32_t offscreen;
  uint16_t offsize;
} __attribute__((packed)) VESAModeInfo;

typedef struct {
  uint8_t signature[4];
  uint16_t Version;
  ReadModeFarPointer oemString;
  uint32_t capabilities;
  ReadModeFarPointer videoModes;
  uint16_t totalMemory;
  uint16_t OEMVersion;
  ReadModeFarPointer vendor;
  ReadModeFarPointer product;
  ReadModeFarPointer revision;
  uint16_t modeCount;
  uint8_t reserved0[220];
  uint8_t oemUse[256];
  VESAModeInfo modeList[0];
} __attribute__((packed)) VESAControllerInfo;

struct VBEINFO {
  char res1[18];
  int16_t xsize, ysize;
  char res2[18];
  int32_t vram;
};

#define rmfarptr2ptr(x) (((uintptr_t)(x).seg << 4) + (x).offset)

#endif
