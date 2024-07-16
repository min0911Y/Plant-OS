// // #include <stdio.h>
// // #include <SDL.h>
// // #include <SDL_image.h>

// // #define WIDTH 500
// // #define HEIGHT 500
// // #define IMG_PATH "E:\\res\\th.svg"

// // int main (int argc, char *argv[]) {

// // 	// variable declarations
// // 	SDL_Window *win = NULL;
// // 	SDL_Renderer *renderer = NULL;
// // 	SDL_Texture *img = NULL;
// // 	int w, h; // texture width & height

// // 	// Initialize SDL.
// // 	if (SDL_Init(SDL_INIT_VIDEO) < 0)
// // 			return 1;

// // 	// create the window and renderer
// // 	// note that the renderer is accelerated
// // 	win = SDL_CreateWindow("Image Loading", 100, 100, WIDTH, HEIGHT, 0);
// // 	renderer = SDL_CreateRenderer(win, -1,0);

// // 	// load our image
// //     printf("load %s\n",IMG_PATH);
// // 	img = IMG_LoadTexture(renderer, IMG_PATH);
// //     printf("%s\n",SDL_GetError());
// // 	SDL_QueryTexture(img, NULL, NULL, &w, &h); // get the width and height
// // of the texture
// // 	// put the location where we want the texture to be drawn into a
// // rectangle
// // 	// I'm also scaling the texture 2x simply by setting the width and
// // height 	SDL_Rect texr; texr.x = 0; texr.y = 0; texr.w = WIDTH; texr.h =
// // HEIGHT;

// // 	// main loop
// // 	while (1) {

// // 		// event handling
// // 		SDL_Event e;
// // 		if ( SDL_PollEvent(&e) ) {
// // 			if (e.type == SDL_QUIT)
// // 				break;
// // 			else if (e.type == SDL_KEYUP && e.key.keysym.sym ==
// // SDLK_ESCAPE) 				break;
// // 		}

// // 		// clear the screen
// // 		SDL_RenderClear(renderer);
// // 		// copy the texture to the rendering context
// // 		SDL_RenderCopy(renderer, img, NULL, &texr);
// // 		// flip the backbuffer
// // 		// this means that everything that we prepared behind the
// // screens is actually shown 		SDL_RenderPresent(renderer);

// // 	}

// // 	SDL_DestroyTexture(img);
// // 	SDL_DestroyRenderer(renderer);
// // 	SDL_DestroyWindow(win);

// // 	return 0;
// // }

// // #include <stdio.h>
// // #include <stdlib.h>
// // #include <syscall.h>
// // #include <string.h>
// // int j = 0;
// // int tid;
// // void hello() {
// //     while(1) {
// //         int k = MessageLength(tid);
// //         if(k != -1) {
// //             char d[6]= {0};
// //             GetMessage(d,tid);
// //             if(strcmp(d,"hello") == 0) {
// //                 ++j;
// //             }
// //         }
// //     }
// // }
// // int main() {
// //     tid = NowTaskID();
// //   int i = AddThread("", hello, (unsigned)malloc(16 * 1024) + 16 * 1024);
  
// //   printf("tid = %d\n", i);
// //   while(1) {
// //     printf("%d\n",j);
// //     SendMessage(i,(void *)"hello",5);
// //     goto_xy(0,0);
// //   }
// // }


// #include<stdio.h>
// #include<math.h>
// #include<string.h>
// int main() {
//     float A = 0, B = 0;
//     float i, j;
//     int k;
//     float z[1760];
//     char b[1760];
//     printf("\x1b[2J");
//     for(;;) {
//         memset(b,32,1760);
//         memset(z,0,7040);
//         for(j=0; j < 6.28; j += 0.07) {
//             for(i=0; i < 6.28; i += 0.02) {
//                 float c = sin(i);
//                 float d = cos(j);
//                 float e = sin(A);
//                 float f = sin(j);
//                 float g = cos(A);
//                 float h = d + 2;
//                 float D = 1 / (c * h * e + f * g + 5);
//                 float l = cos(i);
//                 float m = cos(B);
//                 float n = sin(B);
//                 float t = c * h * g - f * e;
//                 int x = 40 + 30 * D * (l * h * m - t * n);
//                 int y= 12 + 15 * D * (l * h * n + t * m);
//                 int o = x + 80 * y;
//                 int N = 8 * ((f * e - c * d * g) * m - c * d * e - f * g - l * d * n);
//                 if(22 > y && y > 0 && x > 0 && 80 > x && D > z[o]) {
//                     z[o] = D;
//                     b[o] = ".,-~:;=!*#$@"[N > 0 ? N : 0];
//                 }
//             }
//         }
//         printf("\x1b[H");
//         for(k = 0; k < 1761; k++) {
//             putchar(k % 80 ? b[k] : 10);
//             A += 0.00004;
//             B += 0.00002;
//         }
//         sleep(16);
//     }
//     return 0;
// }
 
// void*memset(void*s,int ch,size_t n);
// void usleep(int micro_seconds);




// #include <stdio.h>
// #include <syscall.h>
// #include <signal.h>
// void e() {
//     exit(0);
// }
// int main(int argc, char const *argv[])
// {
//     signal(0,e);
//     printf("\e[1;32mCONGRTULATIONS!\e[m \e[1;37mYou've passed the test!\e[m Hello, World!");
//     return 0;
// }

#include <stdio.h>
#include <SDL.h>
#include <SDL_ttf.h>
 
#define WINDOW_W 800
#define WINDOW_H 640
 
#undef main


int main(int argc,char* argv[])
{
	/*SDL初始化*/
	SDL_Init(SDL_INIT_VIDEO);
	/*TTF初始化*/
	TTF_Init();
	/*创建窗口*/
	SDL_Window *window = SDL_CreateWindow("SDL SHOW TEXT", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
	/*创建渲染器*/
	SDL_Renderer *render = SDL_CreateRenderer(window, -1, 0);
	/*设置渲染器颜色*/
	SDL_SetRenderDrawColor(render, 255, 255, 255, 255);
	/*清空渲染器*/
	SDL_RenderClear(render);
	/*打开字库*/
	TTF_Font *ttffont = TTF_OpenFont("font.ttf", 50);
	if (ttffont == NULL)
	{
		printf("font.ttf open failed\n");
		return 0;
	}
 
	/*字体颜色RGBA*/
	SDL_Color color = { 52,203,120,255 };
	SDL_Color bgColor = { 33,33,33,255 };
 
	/*设置字体大小*/
	TTF_SetFontSize(ttffont, 60);
	/*字体加粗*/
	TTF_SetFontStyle(ttffont, TTF_STYLE_NORMAL);
	/*创建字体显示表面*/
	SDL_Surface *text1_surface = TTF_RenderUTF8_Blended(ttffont, "Genshin Impact, Start!", color);
	/*创建纹理*/
	SDL_Texture * texture = SDL_CreateTextureFromSurface(render, text1_surface);
	/*将surface拷贝到渲染器*/
	SDL_Rect dstrect;
	dstrect.x = WINDOW_W / 2 - text1_surface->w / 2;/*显示的起始位置*/
	dstrect.y = 100;/*显示的起始位置*/
	dstrect.w = text1_surface->w;/*显示的宽度*/
	dstrect.h = text1_surface->h;/*显示的高度*/
 
	/*创建字体显示表面*/
	//SDL_Surface *text1_surface = TTF_RenderUTF8_Blended(ttffont, "Hello,SDL!", color);
	Uint16 msg[10] = { 0x4F60,0x597D,',','T','T','F',0 }; //=Unicode 编码:你好
	SDL_Surface *text2_surface = TTF_RenderUNICODE_Solid(ttffont, msg, color);
	/*创建纹理*/
	SDL_Texture * texture2 = SDL_CreateTextureFromSurface(render, text2_surface);
	/*将surface拷贝到渲染器*/
	SDL_Rect dstrect2;
	dstrect2.x = WINDOW_W / 2 - text2_surface->w / 2;/*显示的起始位置*/
	dstrect2.y = 180;/*显示的起始位置*/
	dstrect2.w = text2_surface->w;/*显示的宽度*/
	dstrect2.h = text2_surface->h;/*显示的高度*/
 
	bool bQuit = false;
	SDL_Event windowEvent;
	while (!bQuit) {
		while (SDL_PollEvent(&windowEvent)) {
			switch (windowEvent.type) {
			case SDL_QUIT:
				bQuit = true;
				break;
			default:
				break;
			}
		}
 
		SDL_RenderClear(render);
		SDL_RenderCopy(render, texture, NULL, &dstrect);
		SDL_RenderCopy(render, texture2, NULL, &dstrect2);
		SDL_RenderPresent(render);
	}
 
	SDL_FreeSurface(text1_surface);/*释放surface*/
	SDL_DestroyTexture(texture);/*释放纹理*/
	TTF_CloseFont(ttffont);
	TTF_Quit();
    SDL_DestroyWindow(window);
	return 0;
}
