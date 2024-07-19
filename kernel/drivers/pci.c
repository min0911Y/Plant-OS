#include <drivers.h>
#define PCI_COMMAND_PORT 0xCF8
#define PCI_DATA_PORT    0xCFC
#define mem_mapping      0
#define input_output     1
typedef struct base_address_register {
  int      prefetchable;
  uint8_t *address;
  uint32_t size;
  int      type;
} base_address_register;
uint32_t read_pci(uint8_t bus, uint8_t device, uint8_t function, uint8_t registeroffset) {
  uint32_t id = 1 << 31 | ((bus & 0xff) << 16) | ((device & 0x1f) << 11) |
                ((function & 0x07) << 8) | (registeroffset & 0xfc);
  io_out32(PCI_COMMAND_PORT, id);
  uint32_t result = io_in32(PCI_DATA_PORT);
  return result >> (8 * (registeroffset % 4));
}
uint32_t read_bar_n(uint8_t bus, uint8_t device, uint8_t function, uint8_t bar_n) {
  uint32_t bar_offset = 0x10 + 4 * bar_n;
  return read_pci(bus, device, function, bar_offset);
}
void write_pci(uint8_t bus, uint8_t device, uint8_t function, uint8_t registeroffset,
               uint32_t value) {
  uint32_t id = 1 << 31 | ((bus & 0xff) << 16) | ((device & 0x1f) << 11) |
                ((function & 0x07) << 8) | (registeroffset & 0xfc);
  io_out32(PCI_COMMAND_PORT, id);
  io_out32(PCI_DATA_PORT, value);
}
uint32_t pci_read_command_status(uint8_t bus, uint8_t slot, uint8_t func) {
  return read_pci(bus, slot, func, 0x04);
}
// write command status register
void pci_write_command_status(uint8_t bus, uint8_t slot, uint8_t func, uint32_t value) {
  write_pci(bus, slot, func, 0x04, value);
}
base_address_register get_base_address_register(uint8_t bus, uint8_t device, uint8_t function,
                                                uint8_t bar) {
  base_address_register result;

  uint32_t headertype = read_pci(bus, device, function, 0x0e) & 0x7e;
  int      max_bars   = 6 - 4 * headertype;
  if (bar >= max_bars) return result;

  uint32_t bar_value = read_pci(bus, device, function, 0x10 + 4 * bar);
  result.type        = (bar_value & 1) ? input_output : mem_mapping;

  if (result.type == mem_mapping) {
    switch ((bar_value >> 1) & 0x3) {
    case 0: // 32
    case 1: // 20
    case 2: // 64
      break;
    }
    result.address      = (uint8_t *)(bar_value & ~0x3);
    result.prefetchable = 0;
  } else {
    result.address      = (uint8_t *)(bar_value & ~0x3);
    result.prefetchable = 0;
  }
  return result;
}
uint8_t pci_get_drive_irq(uint8_t bus, uint8_t slot, uint8_t func) {
  return (uint8_t)read_pci(bus, slot, func, 0x3c);
}
uint32_t pci_get_port_base(uint8_t bus, uint8_t slot, uint8_t func) {
  uint32_t io_port = 0;
  for (int i = 0; i < 6; i++) {
    base_address_register bar = get_base_address_register(bus, slot, func, i);
    if (bar.type == input_output) { io_port = (uint32_t)bar.address; }
  }
  return io_port;
}
void PCI_GET_DEVICE(uint16_t vendor_id, uint16_t device_id, uint8_t *bus, uint8_t *slot,
                    uint8_t *func) {
  extern int PCI_ADDR_BASE;
  u8        *pci_drive = PCI_ADDR_BASE;
  for (;; pci_drive += 0x110 + 4) {
    if (pci_drive[0] == 0xff) {
      struct pci_config_space_public *pci_config_space_puclic;
      pci_config_space_puclic = (struct pci_config_space_public *)(pci_drive + 0x0c);
      if (pci_config_space_puclic->VendorID == vendor_id &&
          pci_config_space_puclic->DeviceID == device_id) {
        *bus  = pci_drive[1];
        *slot = pci_drive[2];
        *func = pci_drive[3];
        return;
      }
    } else {
      break;
    }
  }
}
void pci_config(u32 bus, u32 f, u32 equipment, u32 adder) {
  u32 cmd = 0;
  cmd     = 0x80000000 + (u32)adder + ((u32)f << 8) + ((u32)equipment << 11) + ((u32)bus << 16);
  // cmd = cmd | 0x01;
  io_out32(PCI_COMMAND_PORT, cmd);
}
void init_PCI(u32 adder_Base) {
  u32 i, BUS, Equipment, F, ADDER, *i1;
  u8 *PCI_DATA = adder_Base, *PCI_DATA1;
  for (BUS = 0; BUS < 256; BUS++) {                    //查询总线
    for (Equipment = 0; Equipment < 32; Equipment++) { //查询设备
      for (F = 0; F < 8; F++) {                        //查询功能
        pci_config(BUS, F, Equipment, 0);
        if (io_in32(PCI_DATA_PORT) != 0xFFFFFFFF) {
          //当前插槽有设备
          //把当前设备信息映射到PCI数据区
          int key = 1;
          while (key) {
            //此配置表为空
            // printk("PCI_DATA:%x\n", PCI_DATA);
            // getch();
            PCI_DATA1  = PCI_DATA;
            *PCI_DATA1 = 0xFF; //表占用标志
            PCI_DATA1++;
            *PCI_DATA1 = BUS; //总线号
            PCI_DATA1++;
            *PCI_DATA1 = Equipment; //设备号
            PCI_DATA1++;
            *PCI_DATA1 = F; //功能号
            PCI_DATA1++;
            PCI_DATA1 = PCI_DATA1 + 8;
            //写入寄存器配置
            for (ADDER = 0; ADDER < 256; ADDER = ADDER + 4) {
              pci_config(BUS, F, Equipment, ADDER);
              i  = io_in32(PCI_DATA_PORT);
              i1 = i;
              //*i1 = PCI_DATA1;
              memcpy(PCI_DATA1, &i, 4);
              PCI_DATA1 = PCI_DATA1 + 4;
            }
            for (uint8_t barNum = 0; barNum < 6; barNum++) {
              base_address_register bar = get_base_address_register(BUS, Equipment, F, barNum);
              if (bar.address && (bar.type == input_output)) {
                PCI_DATA1 += 4;
                int i      = ((uint32_t)(bar.address));
                memcpy(PCI_DATA1, &i, 4);
              }
            }
            /*PCI_DATA += 12;
            struct PCI_CONFIG_SPACE_PUCLIC *PCI_CONFIG_SPACE = (struct
            PCI_CONFIG_SPACE_PUCLIC *)PCI_DATA; PCI_DATA -= 12;
            printk("PCI_CONFIG_SPACE:%08x\n", PCI_CONFIG_SPACE);
            printk("PCI_CONFIG_SPACE->VendorID:%08x\n",
            PCI_CONFIG_SPACE->VendorID);
            printk("PCI_CONFIG_SPACE->DeviceID:%08x\n",
            PCI_CONFIG_SPACE->DeviceID);
            printk("PCI_CONFIG_SPACE->Command:%08x\n",
            PCI_CONFIG_SPACE->Command);
            printk("PCI_CONFIG_SPACE->Status:%08x\n", PCI_CONFIG_SPACE->Status);
            printk("PCI_CONFIG_SPACE->RevisionID:%08x\n",
            PCI_CONFIG_SPACE->RevisionID);
            printk("PCI_CONFIG_SPACE->ProgIF:%08x\n", PCI_CONFIG_SPACE->ProgIF);
            printk("PCI_CONFIG_SPACE->SubClass:%08x\n",
            PCI_CONFIG_SPACE->SubClass);
            printk("PCI_CONFIG_SPACE->BaseCode:%08x\n",
            PCI_CONFIG_SPACE->BaseClass);
            printk("PCI_CONFIG_SPACE->CacheLineSize:%08x\n",
            PCI_CONFIG_SPACE->CacheLineSize);
            printk("PCI_CONFIG_SPACE->LatencyTimer:%08x\n",
            PCI_CONFIG_SPACE->LatencyTimer);
            printk("PCI_CONFIG_SPACE->HeaderType:%08x\n",
            PCI_CONFIG_SPACE->HeaderType);
            printk("PCI_CONFIG_SPACE->BIST:%08x\n", PCI_CONFIG_SPACE->BIST);
            printk("PCI_CONFIG_SPACE->BaseAddr0:%08x\n",
            PCI_CONFIG_SPACE->BaseAddr[0]);
            printk("PCI_CONFIG_SPACE->BaseAddr1:%08x\n",
            PCI_CONFIG_SPACE->BaseAddr[1]);
            printk("PCI_CONFIG_SPACE->BaseAddr2:%08x\n",
            PCI_CONFIG_SPACE->BaseAddr[2]);
            printk("PCI_CONFIG_SPACE->BaseAddr3:%08x\n",
            PCI_CONFIG_SPACE->BaseAddr[3]);
            printk("PCI_CONFIG_SPACE->BaseAddr4:%08x\n",
            PCI_CONFIG_SPACE->BaseAddr[4]);
            printk("PCI_CONFIG_SPACE->BaseAddr5:%08x\n",
            PCI_CONFIG_SPACE->BaseAddr[5]);
            printk("PCI_CONFIG_SPACE->CardbusCISPtr:%08x\n",
            PCI_CONFIG_SPACE->CardbusCIS);
            printk("PCI_CONFIG_SPACE->SubsystemVendorID:%08x\n",
            PCI_CONFIG_SPACE->SubVendorID);
            printk("PCI_CONFIG_SPACE->SubsystemID:%08x\n",
            PCI_CONFIG_SPACE->SubSystemID);
            printk("PCI_CONFIG_SPACE->ExpansionROMBaseAddr:%08x\n",
            PCI_CONFIG_SPACE->ROMBaseAddr);
            printk("PCI_CONFIG_SPACE->CapabilitiesPtr:%08x\n",
            PCI_CONFIG_SPACE->CapabilitiesPtr);
            printk("PCI_CONFIG_SPACE->Reserved1:%08x\n",
            PCI_CONFIG_SPACE->Reserved[0]);
            printk("PCI_CONFIG_SPACE->Reserved2:%08x\n",
            PCI_CONFIG_SPACE->Reserved[1]);
            printk("PCI_CONFIG_SPACE->InterruptLine:%08x\n",
            PCI_CONFIG_SPACE->InterruptLine);
            printk("PCI_CONFIG_SPACE->InterruptPin:%08x\n",
            PCI_CONFIG_SPACE->InterruptPin);
            printk("PCI_CONFIG_SPACE->MinGrant:%08x\n",
            PCI_CONFIG_SPACE->MinGrant);
            printk("PCI_CONFIG_SPACE->MaxLatency:%08x\n",
            PCI_CONFIG_SPACE->MaxLatency); for (int i = 0; i < 272+4; i++)
            {
                printk("%02x ", PCI_DATA[i]);
            }
            printk("\n");*/
            PCI_DATA = PCI_DATA + 0x110 + 4;
            key      = 0;
          }
        }
      }
    }
  }
  //函数执行完PCI_DATA就是PCI设备表的结束地址
}
void PCI_ClassCode_Print(struct pci_config_space_public *pci_config_space_puclic) {
  u8 *pci_drive = (u8 *)pci_config_space_puclic - 12;
  printk("BUS:%02x ", pci_drive[1]);
  printk("EQU:%02x ", pci_drive[2]);
  printk("F:%02x ", pci_drive[3]);
  printk("IO Port:%08x ", pci_get_port_base(pci_drive[1], pci_drive[2], pci_drive[3]));
  printk("IRQ Line:%02x ", pci_get_drive_irq(pci_drive[1], pci_drive[2], pci_drive[3]));
  if (pci_config_space_puclic->BaseClass == 0x0) {
    printk("Nodefined ");
    if (pci_config_space_puclic->SubClass == 0x0)
      printk("Non-VGA-Compatible Unclassified Device\n");
    else if (pci_config_space_puclic->SubClass == 0x1)
      printk("VGA-Compatible Unclassified Device\n");
  } else if (pci_config_space_puclic->BaseClass == 0x1) {
    printk("Mass Storage Controller ");
    if (pci_config_space_puclic->SubClass == 0x0)
      printk("SCSI Bus Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x1)
      printk("IDE Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x2)
      printk("Floppy Disk Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x3)
      printk("IPI Bus Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x4)
      printk("RAID Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x5)
      printk("ATA Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x6)
      printk("Serial ATA Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x7)
      printk("Serial Attached SCSI Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x8)
      printk("Non-Volatile Memory Controller\n");
    else
      printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x2) {
    printk("Network Controller ");
    if (pci_config_space_puclic->SubClass == 0x0)
      printk("Ethernet Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x1)
      printk("Token Ring Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x2)
      printk("FDDI Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x3)
      printk("ATM Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x4)
      printk("ISDN Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x5)
      printk("WorldFip Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x6)
      printk("PICMG 2.14 Multi Computing Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x7)
      printk("Infiniband Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x8)
      printk("Fabric Controller\n");
    else
      printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x3) {
    printk("Display Controller ");
    if (pci_config_space_puclic->SubClass == 0x0)
      printk("VGA Compatible Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x1)
      printk("XGA Controller\n");
    else if (pci_config_space_puclic->SubClass == 0x2)
      printk("3D Controller (Not VGA-Compatible)\n");
    else
      printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x4) {
    printk("Multimedia Controller ");
    printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x5) {
    printk("Memory Controller ");
    printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x6) {
    printk("Bridge ");
    if (pci_config_space_puclic->SubClass == 0x0)
      printk("Host Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x1)
      printk("ISA Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x2)
      printk("EISA Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x3)
      printk("MCA Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x4 || pci_config_space_puclic->SubClass == 0x9)
      printk("PCI-to-PCI Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x5)
      printk("PCMCIA Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x6)
      printk("NuBus Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x7)
      printk("CardBus Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0x8)
      printk("RACEway Bridge\n");
    else if (pci_config_space_puclic->SubClass == 0xA)
      printk("InfiniBand-to-PCI Host Bridge\n");
    else
      printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x7) {
    printk("Simple Communication Controller ");
    printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x8) {
    printk("Base System Peripheral ");
    printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0x9) {
    printk("Input Device Controller ");
    printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0xA) {
    printk("Docking Station ");
    printk("\n");
  } else if (pci_config_space_puclic->BaseClass == 0xB) {
    printk("Processor ");
    printk("\n");
  } else {
    printk("Unknow\n");
  }
}