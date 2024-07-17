#include <define.h>
#include <osapi.h>
#include <pl2d/fb.h>
#include <plds/server/api.h>
#include <syscall.h>
#include <time.h>
#include <type.h>

void __gxx_personality_v0() {
  exit(-1);
}

void usleep(uint64_t time_us) {
  time_ns_t end_time;
  gettime_ns(&end_time);
  end_time.sec  = time_us / 1000000;
  end_time.nsec = time_us % 1000000 * 1000;
  if (end_time.nsec > 1000000000) {
    end_time.sec  += 1;
    end_time.nsec -= 1000000000;
  }
  time_ns_t now_time;
  do {
    gettime_ns(&now_time);
  } while (now_time.sec < end_time.sec || now_time.nsec < end_time.nsec);
}

u32 screen_width;
u32 screen_height;

bool exit_flag = false;

void program_exit() {
  exit_flag = true;
}

void screen_flush() {
  static time_ns_t old_time = {};
  time_ns_t        time;
  u64              frame_time;
  while (true) {
    gettime_ns(&time);
    frame_time = (time.sec - old_time.sec) * 1000000000 + (time.nsec - old_time.nsec);
    if (frame_time >= 16666667) break;
    usleep((16666667 - frame_time) / 1000);
  }
  logkf("%lf\n", 1e9 / frame_time);
  old_time = time;
}

int main() {
  screen_height = 768;
  screen_width  = 1024;

  plds_init((void *)set_mode(screen_width, screen_height), screen_width, screen_height,
            pl2d_PixFmt_ARGB);

  while (true) {
    plds_flush();
  }

  return 0;
}
