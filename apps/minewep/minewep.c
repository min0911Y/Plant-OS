// 简易扫雷练习代码 by zhouzhihao
// Powered by SDL2 & SDL2_image
// 2024.2.23
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <SDL.h>
#include <SDL_image.h>
#define COUNT_OF_BLOCKS 16
#define COUNT_OF_MINES 40
struct stack {	// 用来向四周扫的栈
  int top;
  int *buffer;
  int size;
};
static int block_type[COUNT_OF_BLOCKS][COUNT_OF_BLOCKS];	// 方块属性：-1.点了没雷 0.无雷没扫/点 1.有雷没扫/点 2.扫了没雷 3.扫了有雷
void putblock(SDL_Window *window, SDL_Surface *surface, SDL_Surface *png, int x, int y) {
  SDL_Rect rect;
  rect.x = x * 16;
  rect.y = y * 16;
  SDL_BlitSurface(png, NULL, surface, &rect);
  return;
}
int how_many_mines_around(int x, int y) {
  int n = 0;
  if (block_type[y-1][x] == 1 && y > 0) n++;
  if (block_type[y-1][x-1] == 1 && y > 0 && x > 0) n++;
  if (block_type[y-1][x+1] == 1 && y > 0 && x < COUNT_OF_BLOCKS - 1) n++;
  if (block_type[y+1][x] == 1 && y < COUNT_OF_BLOCKS - 1) n++;
  if (block_type[y+1][x-1] == 1 && y < COUNT_OF_BLOCKS - 1 && x > 0) n++;
  if (block_type[y+1][x+1] == 1 && y < COUNT_OF_BLOCKS - 1 && x < COUNT_OF_BLOCKS - 1) n++;
  if (block_type[y][x-1] == 1 && x > 0) n++;
  if (block_type[y][x+1] == 1 && x < COUNT_OF_BLOCKS - 1) n++;
  // 已经扫了的雷也要算
  if (block_type[y-1][x] == 3 && y > 0) n++;
  if (block_type[y-1][x-1] == 3 && y > 0 && x > 0) n++;
  if (block_type[y-1][x+1] == 3 && y > 0 && x < COUNT_OF_BLOCKS - 1) n++;
  if (block_type[y+1][x] == 3 && y < COUNT_OF_BLOCKS - 1) n++;
  if (block_type[y+1][x-1] == 3 && y < COUNT_OF_BLOCKS - 1 && x > 0) n++;
  if (block_type[y+1][x+1] == 3 && y < COUNT_OF_BLOCKS - 1 && x < COUNT_OF_BLOCKS - 1) n++;
  if (block_type[y][x-1] == 3 && x > 0) n++;
  if (block_type[y][x+1] == 3 && x < COUNT_OF_BLOCKS - 1) n++;
  return n;
}
void push(struct stack *stk, int data) {
  if (stk->size <= stk->top * sizeof(int)) {
	if (stk->buffer != NULL) free(stk->buffer);
	stk->buffer = (int *)malloc(stk->size + 128 * sizeof(int));
	stk->size += 128 * sizeof(int);
  }
  stk->buffer[stk->top++] = data;
  return;
}
int pop(struct stack *stk) {
  return stk->buffer[--stk->top];
}
int main(int argc, char *argv[]) {
  // 初始化SDL2
  if (SDL_Init(SDL_INIT_VIDEO) == -1) {
	printf("SDL init failed\n");
	return 0;
  }
  // 创建窗口
  system("cls");
  printf("Minesweeper by zhouzhihao v0.1\n");
  SDL_Window *window = SDL_CreateWindow("Minesweeper", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        16 * COUNT_OF_BLOCKS, 16 * COUNT_OF_BLOCKS, SDL_WINDOW_SHOWN);
  SDL_Surface *surface = SDL_GetWindowSurface(window);
  
  // 加载素材
  SDL_Surface *png_block = IMG_Load("pngs/block.png");
  SDL_Surface *png_mine = IMG_Load("pngs/mine.png");
  SDL_Surface *png_flag = IMG_Load("pngs/flag.png");
  
  // 布置背景
  for (int i = 0; i != COUNT_OF_BLOCKS; i++) {
	for (int j = 0; j != COUNT_OF_BLOCKS; j++) {
	  putblock(window, surface, png_block, i, j);
	  block_type[i][j] = 0;
	}
  }
  
  // 随机雷的坐标
  int count_of_mines = COUNT_OF_MINES;
  srand(time(NULL));
  for (int i = 0; i != COUNT_OF_MINES; i++) {
	int x = rand() % COUNT_OF_BLOCKS;
	int y = rand() % COUNT_OF_BLOCKS;
	if (block_type[y][x] == 1) { i--; continue; }
	block_type[y][x] = 1;
  }
  printf("\e[1;32mcount of mines:\e[m %d\n", COUNT_OF_MINES);
  
  SDL_Event event;
  bool quit = false, end = false;
  while (!quit) {
	if (count_of_mines == 0 && !end) {	// 扫完雷了
	  printf("Congratulation! You sweep all mines and win the game!\n");
	  end = true;
	}
	while (SDL_PollEvent(&event)) {	// 消息循环
	  if (event.type == SDL_QUIT) {	// 按下关闭窗口
		quit = true;
	  } else if (event.type == SDL_MOUSEBUTTONDOWN && !end) {	// 按下鼠标且游戏没结束
	    int x = event.motion.x / 16;
		int y = event.motion.y / 16;
		if (event.button.button == SDL_BUTTON_LEFT) {	// 按下鼠标左键
		  if (block_type[y][x] == 0) {
			block_type[y][x] = -1;
			char name[32];
			sprintf(name, "pngs/%d.png", how_many_mines_around(x, y));	// 根据四周雷的个数来确定素材
			SDL_Surface *png_n = IMG_Load(name);
		    putblock(window, surface, png_n, x, y);
			// 如果四周没雷 尝试向四周连续扫
			if (how_many_mines_around(x, y) == 0) {
			  // 初始化一个栈
			  struct stack stk;
			  stk.buffer = NULL;
			  stk.size = 0;
			  stk.top = 0;
			  // 将点击的坐标推入栈中（以点击的坐标为中心 向四周扫）
			  push(&stk, x);
			  push(&stk, y);
			  push(&stk, 0);
			  while (stk.top != 0) {  // 这么写是因为有标记以扫过的区域 所以不会扫出死循环
				int flag = pop(&stk); // FLAG用于标记不用重复扫的区域（速度更快）
				y = pop(&stk);
				x = pop(&stk);
				if (flag != 1) { // 横扫的两个循环
			      for (int i = x + 1; i <= COUNT_OF_BLOCKS; i++) {
				    if (block_type[y][i] == 0 && how_many_mines_around(i, y) == 0) {
				      block_type[y][i] = -1;
				      putblock(window, surface, png_n, i, y);
					  // 因为是横扫得到的坐标 所以在递归到以它为中心时不需要横扫
					  push(&stk, i);
					  push(&stk, y);
					  push(&stk, 1);
				    } else {
				      break;
				    }
			      }
			      for (int i = x - 1; i >= 0; i--) {
				    if (block_type[y][i] == 0 && how_many_mines_around(i, y) == 0) {
				      block_type[y][i] = -1;
				      putblock(window, surface, png_n, i, y);
					  push(&stk, i);
					  push(&stk, y);
					  push(&stk, 1);
				    } else {
				      break;
				    }
				  }
				} else if (flag != 2) {  // 竖扫的两个循环
			      for (int i = y + 1; i <= COUNT_OF_BLOCKS; i++) {
				    if (block_type[i][x] == 0 && how_many_mines_around(x, i) == 0) {
				      block_type[i][x] = -1;
				      putblock(window, surface, png_n, x, i);
					  push(&stk, x);
					  push(&stk, i);
					  push(&stk, 2);
				    } else {
				      break;
				    }
			      }
			      for (int i = y - 1; i >= 0; i--) {
				    if (block_type[i][x] == 0 && how_many_mines_around(x, i) == 0) {
				      block_type[i][x] = -1;
				      putblock(window, surface, png_n, x, i);
					  push(&stk, x);
					  push(&stk, i);
					  push(&stk, 2);
				    } else {
				      break;
				    }
			      }
				}
			  }
			}
			SDL_FreeSurface(png_n);
		  } else if (block_type[y][x] == 1) {	// 踩雷
			putblock(window, surface, png_mine, x, y);
			printf("Oops! You step on a mine, Game Over.\n");
			end = true;
		  }
		} else if (event.button.button == SDL_BUTTON_RIGHT) {	// 按下鼠标右键
		  if (block_type[y][x] == 0) {
		    block_type[y][x] = 2;
		    putblock(window, surface, png_flag, x, y);
		  } else if (block_type[y][x] == 1) {	// 成功扫到雷
			block_type[y][x] = 3;
			putblock(window, surface, png_flag, x, y);
			count_of_mines--;
			printf("You set a flag, count of mines: %d\n", count_of_mines);
		  } else if (block_type[y][x] == 2) {
			block_type[y][x] = 0;
			putblock(window, surface, png_block, x, y);
		  }
		}
	  }
	}
	SDL_Delay(5);	// 防止CPU使用率过高
	SDL_UpdateWindowSurface(window);
  }
  
  SDL_FreeSurface(png_block);
  SDL_FreeSurface(png_mine);
  SDL_FreeSurface(png_flag);
  SDL_FreeSurface(surface);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}