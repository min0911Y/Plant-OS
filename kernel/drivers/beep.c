#include <arch/x86/io.h>
#include <drivers.h>

enum {
  PIT_CHANNEL2 = 0x42,
  PIT_COMMAND = 0x43,
  PIT_CHANNEL2_SQUARE_WAVE = 0xb6, /* channel 2, mode 3, 16 位二进制计数 */
  PIT_INPUT_HZ = 1193182,
  SPEAKER_CONTROL = 0x61,
  SPEAKER_GATE = 0x03, /* bit0 打开 PIT 门控，bit1 接通扬声器 */
  BEEP_UNIT_MS = 50,   /* dup 的单位时长 */
  BEEP_OCTAVES = 3,
  BEEP_NOTES = 7,
};

/* point 选择音区，notes 选择音级，沿用既有的 1..3 / 1..7 编号。 */
static const uint16_t beep_scale[BEEP_OCTAVES][BEEP_NOTES] = {
    {138, 147, 165, 175, 196, 220, 247},
    {524, 587, 659, 698, 784, 880, 988},
    {262, 294, 330, 349, 392, 440, 494},
};

void beep(int point, int notes, int dup) {
  if (point < 1 || point > BEEP_OCTAVES || notes < 1 || notes > BEEP_NOTES ||
      dup <= 0) {
    return;
  }

  uint16_t divisor = PIT_INPUT_HZ / beep_scale[point - 1][notes - 1];
  x86_port_write8(PIT_COMMAND, PIT_CHANNEL2_SQUARE_WAVE);
  x86_port_write8(PIT_CHANNEL2, divisor & 0xff);
  x86_port_write8(PIT_CHANNEL2, divisor >> 8);

  uint8_t control = x86_port_read8(SPEAKER_CONTROL);
  x86_port_write8(SPEAKER_CONTROL, control | SPEAKER_GATE);
  sleep((unsigned long long)dup * BEEP_UNIT_MS);
  x86_port_write8(SPEAKER_CONTROL, control & ~SPEAKER_GATE);
}
