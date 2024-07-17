#include <define.h>
#include <osapi.h>
#include <pl2d/fb.h>
#include <plds/server/api.h>
#include <syscall.h>
#include <time.h>
#include <type.h>

void __gxx_personality_v0()
{
  exit (-1);
}

u32 screen_width;
u32 screen_height;

bool exit_flag = false;

void program_exit() {
  exit_flag = true;
}

void screen_flush() {
  static u64 old_time = 0;
  u64        time, frame_time;
  while (true) {
    time       = clock() * (u64)1000;
    frame_time = time - old_time;
    if (frame_time >= 16667) break;
    sleep((16667 - frame_time) / 1000);
  }
  old_time = time;
}

int main() {
  screen_height = 768;
  screen_width  = 1024;

  plds_init((void*)set_mode(screen_width, screen_height), screen_width, screen_height, pl2d_PixFmt_ARGB);

  while (true) {
    plds_flush();
  }

  return 0;
}
