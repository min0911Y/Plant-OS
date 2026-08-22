#include <string.h>
#include <syscall.h>
#include <stdio.h>
#include <stdlib.h>
void clean(char* s, int l) {
  for (int i = 0; i < l; i++) {
    s[i] = 0;
  }
}
int Week(int yy, int mm, int dd) {
  int year = yy;  //因为定义的是全局变量 所以就需要定义新变量来传递值
  int month = mm;  //当然 你也可以定义在main函数内然后用&引用传递
  int day = dd;
  if (mm < 3) {
    year -= 1;
    month += 12;
  }
  int y = year % 100;
  int c = year / 100;
  int d = day;
  int m = month;
  int w = (y + y / 4 + c / 4 - 2 * c + 13 * (m + 1) / 5 + d - 1) % 7;
  return w;
}
void cal(int year, int month,int d) {
  char *months[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
  int day = 0;
  if (month == 1 || month == 3 || month == 5 || month == 7 || month == 8 ||
      month == 10 || month == 12) {
    day = 31;
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    day = 30;
  } else if (month == 2) {
    if (year % 4 == 0) {
      day = 29;
    } else {
      day = 28;
    }
  }
  char* buf = malloc(128);
  sprintf(buf, "%s %d", months[month-1],year);
  for(int i = 0;i<(20-strlen(buf)) / 2;i++) {
    print(" ");
  }
  print(buf);
  api_free(buf, 128);
  print("\n");
  print("Su Mo Tu We Th Fr Sa\n");
  buf = malloc(128);
  int week = 0;
  int col =  get_cons_color();
  for (int i = 0; i < Week(year, month, 1); i++) {
    print("   ");
    week++;
  }
  week++;
  if(d == 1) {
    set_cons_color(~col);
  }
  print("1 ");
  set_cons_color(col);
  print("  ");
  if (week % 7 == 0) {
    print("\n");
    week = 1;
  } else {
    week++;
  }
  for (int i = 2, k = week; i <= day; i++, k++) {
    if(d == i) {
      set_cons_color(~col);
    }
    sprintf(buf, "%d", i);
    print(buf);
    if (k % 7 == 0) {
      print("\n");
    } else {
      for (int j = 0; j < 2 - strlen(buf) + 1; j++) {
        if(j >= 2 - strlen(buf)) {
          set_cons_color(col);
        }
        print(" ");
      }
    }
    set_cons_color(col);
    clean(buf, 128);
  }
  clean(buf, 128);
  free(buf);
}
int main(int argc, char** argv) {
  cal(get_year(),get_mon_hex(),get_day_of_month());
  return 0;
}