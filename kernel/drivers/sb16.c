#include <arch/x86/io.h>
/*
 * sb 16 driver
 * https://github.com/StevenBaby/onix/blob/dev/src/kernel/sb16.c
 */
#include <arch/x86/interrupt.h>
#include <dos.h>
#include <drivers.h>
#include <irq.h>

struct WAV16_HEADER {
  char riff[4];
  int size;
  char wave[4];
  char fmt[4];
  int fmt_size;
  short format;
  short channel;
  int sample_rate;
  int byte_per_sec;
} __attribute__((packed));
#define SB_MIXER 0x224       // DSP 混合器端口
#define SB_MIXER_DATA 0x225  // DSP 混合器数据端口
#define SB_RESET 0x226       // DSP 重置
#define SB_READ 0x22A        // DSP 读
#define SB_WRITE 0x22C       // DSP 写
#define SB_STATE 0x22E       // DSP 读状态
#define SB_INTR16 0x22F      // DSP 16 位中断响应

#define CMD_STC 0x40   // Set Time Constant
#define CMD_SOSR 0x41  // Set Output Sample Rate
#define CMD_SISR 0x42  // Set Input Sample Rate

#define CMD_SINGLE_IN8 0xC8    // Transfer mode 8bit input
#define CMD_SINGLE_OUT8 0xC0   // Transfer mode 8bit output
#define CMD_SINGLE_IN16 0xB8   // Transfer mode 16bit input
#define CMD_SINGLE_OUT16 0xB0  // Transfer mode 16bit output

#define CMD_AUTO_IN8 0xCE    // Transfer mode 8bit input auto
#define CMD_AUTO_OUT8 0xC6   // Transfer mode 8bit output auto
#define CMD_AUTO_IN16 0xBE   // Transfer mode 16bit input auto
#define CMD_AUTO_OUT16 0xB6  // Transfer mode 16bit output auto

#define CMD_ON 0xD1       // Turn speaker on
#define CMD_OFF 0xD3      // Turn speaker off
#define CMD_SP8 0xD0      // Stop playing 8 bit channel
#define CMD_RP8 0xD4      // Resume playback of 8 bit channel
#define CMD_SP16 0xD5     // Stop playing 16 bit channel
#define CMD_RP16 0xD6     // Resume playback of 16 bit channel
#define CMD_VERSION 0xE1  // Turn speaker off

#define MODE_MONO8 0x00
// #define MODE_STEREO8 0x20
// #define MODE_MONO16 0x10
#define MODE_STEREO16 0x30

#define STATUS_READ 0x80   // read buffer status
#define STATUS_WRITE 0x80  // write buffer status

#define DMA_BUF_ADDR 0x90000  // 不能跨越 64K 边界
#define DMA_BUF_SIZE 0x8000   // 缓冲区长度

#define SAMPLE_RATE 44100  // 采样率

struct sb16 {
  mtask* use_task;
  int status;
  char* addr;       // DMA 地址
  uint8_t mode;     // 模式
  uint8_t channel;  // DMA 通道
  uint8_t flag;
};
struct sb16 sb;
void sb16_remove_task(mtask *task) {
  if (sb.use_task == task) {
    sb.use_task = NULL;
  }
}
void sb16_handler(int* esp) {
  send_eoi(5);

  x86_port_read8(SB_INTR16);

  uint8_t state = x86_port_read8(SB_STATE);

  logk("sb16 handler state 0x%X...\n", state);
  sb.flag = !sb.flag;
  task_run(sb.use_task);
}
void disable_sb16() {
  if (!interrupt_register_entry(IRQ_BASE_VECTOR + SB16_IRQ,
                                asm_sb16_handler)) {
    logk("sb16: invalid interrupt entry\n");
    return;
  }
  sb.addr = (char*)DMA_BUF_ADDR;
  sb.mode = MODE_STEREO16;
  sb.channel = 5;
  sb.use_task = NULL;
  irq_configure(SB16_IRQ, IRQ_TRIGGER_LEVEL, IRQ_POLARITY_LOW);
  irq_mask_clear(SB16_IRQ);
}
static void sb_reset() {
  x86_port_write8(SB_RESET, 1);
  sleep(1);
  x86_port_write8(SB_RESET, 0);
  uint8_t state = x86_port_read8(SB_READ);
  logk("sb16 reset state 0x%x\n", state);
}

static void sb_intr_irq() {
  x86_port_write8(SB_MIXER, 0x80);
  uint8_t data = x86_port_read8(SB_MIXER_DATA);
  if (data != 2) {
    x86_port_write8(SB_MIXER, 0x80);
    x86_port_write8(SB_MIXER_DATA, 0x2);
  }
}

static void sb_out(uint8_t cmd) {
  while (x86_port_read8(SB_WRITE) & 128)
    ;
  x86_port_write8(SB_WRITE, cmd);
}

static void sb_set_volume(uint8_t level) {
  logk("set sb16 volume to 0x%02X\n", level);
  x86_port_write8(SB_MIXER, 0x22);
  x86_port_write8(SB_MIXER_DATA, level);
}

int sb16_set(int cmd, void* args) {
  switch (cmd) {
    // 设置 tty 参数
    case 0:
      while (sb.use_task)
        ;
      irq_state_t state = irq_save();
      sb.use_task = current_task();
      irq_restore(state);
      sb_reset();      // 重置 DSP
      sb_intr_irq();   // 设置中断
      sb_out(CMD_ON);  // 打开声霸卡
      return 0;
    case 1:
      sb_out(CMD_OFF);  // 关闭声霸卡
      sb.use_task = NULL;
      return 0;
    case 2:
      sb.mode = MODE_MONO8;
      sb.channel = 1;
      return 0;
    case 3:
      sb.mode = MODE_STEREO16;
      sb.channel = 5;
      return 0;
    case 4:
      sb_set_volume((uint32_t)args & 0xff);
      return 0;
    default:
      break;
  }
  return -1;
}

int sb16_write(char* data, size_t size) {
  memcpy(sb.addr, data, size);
  // 设置采样率
  sb_out(CMD_SOSR);                   // 44100 = 0xAC44
  sb_out((SAMPLE_RATE >> 8) & 0xFF);  // 0xAC
  sb_out(SAMPLE_RATE & 0xFF);         // 0x44
  dma_xfer(sb.channel, (uintptr_t)sb.addr, size, 0);
  if (sb.mode == MODE_MONO8) {
    sb_out(CMD_SINGLE_OUT8);
    sb_out(MODE_MONO8);
  } else {
    sb_out(CMD_SINGLE_OUT16);
    sb_out(MODE_STEREO16);
    size >>= 2;  // size /= 4
  }

  sb_out((size - 1) & 0xFF);
  sb_out(((size - 1) >> 8) & 0xFF);
  sb.flag = 0;
  while (!sb.flag)
    ;
  return size;
}

void wav_player(char* filename) {
  FILE* fp = fopen(filename, "rb");
  char* buf = malloc(DMA_BUF_SIZE);
  fread(buf, 1, 44, fp);
  sb16_set(0, 0);
  sb16_set(2, 0);
  sb16_set(4, (void *)(uintptr_t)0xff);
  int sz;
  while ((sz = fread(buf, 1, DMA_BUF_SIZE, fp)) != 0) {
    sb16_write(buf, sz);
  }
  sb16_set(1, 0);
  fclose(fp);
}
void wav_player_test() {
  char buf[100];
  input(buf,100);
  wav_player(buf);
}
