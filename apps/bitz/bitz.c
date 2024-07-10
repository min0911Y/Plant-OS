// #include <stdio.h>
// #include <SDL.h>
// #include <SDL_image.h>

// #define WIDTH 500
// #define HEIGHT 500
// #define IMG_PATH "E:\\res\\th.svg"

// int main (int argc, char *argv[]) {

// 	// variable declarations
// 	SDL_Window *win = NULL;
// 	SDL_Renderer *renderer = NULL;
// 	SDL_Texture *img = NULL;
// 	int w, h; // texture width & height

// 	// Initialize SDL.
// 	if (SDL_Init(SDL_INIT_VIDEO) < 0)
// 			return 1;

// 	// create the window and renderer
// 	// note that the renderer is accelerated
// 	win = SDL_CreateWindow("Image Loading", 100, 100, WIDTH, HEIGHT, 0);
// 	renderer = SDL_CreateRenderer(win, -1,0);

// 	// load our image
//     printf("load %s\n",IMG_PATH);
// 	img = IMG_LoadTexture(renderer, IMG_PATH);
//     printf("%s\n",SDL_GetError());
// 	SDL_QueryTexture(img, NULL, NULL, &w, &h); // get the width and height
// of the texture
// 	// put the location where we want the texture to be drawn into a
// rectangle
// 	// I'm also scaling the texture 2x simply by setting the width and
// height 	SDL_Rect texr; texr.x = 0; texr.y = 0; texr.w = WIDTH; texr.h =
// HEIGHT;

// 	// main loop
// 	while (1) {

// 		// event handling
// 		SDL_Event e;
// 		if ( SDL_PollEvent(&e) ) {
// 			if (e.type == SDL_QUIT)
// 				break;
// 			else if (e.type == SDL_KEYUP && e.key.keysym.sym ==
// SDLK_ESCAPE) 				break;
// 		}

// 		// clear the screen
// 		SDL_RenderClear(renderer);
// 		// copy the texture to the rendering context
// 		SDL_RenderCopy(renderer, img, NULL, &texr);
// 		// flip the backbuffer
// 		// this means that everything that we prepared behind the
// screens is actually shown 		SDL_RenderPresent(renderer);

// 	}

// 	SDL_DestroyTexture(img);
// 	SDL_DestroyRenderer(renderer);
// 	SDL_DestroyWindow(win);

// 	return 0;
// }

// #include <stdio.h>
// #include <stdlib.h>
// #include <syscall.h>
// #include <string.h>
// int j = 0;
// int tid;
// void hello() {
//     while(1) {
//         int k = MessageLength(tid);
//         if(k != -1) {
//             char d[6]= {0};
//             GetMessage(d,tid);
//             if(strcmp(d,"hello") == 0) {
//                 ++j;
//             }
//         }
//     }
// }
// int main() {
//     tid = NowTaskID();
//   int i = AddThread("", hello, (unsigned)malloc(16 * 1024) + 16 * 1024);
  
//   printf("tid = %d\n", i);
//   while(1) {
//     printf("%d\n",j);
//     SendMessage(i,(void *)"hello",5);
//     goto_xy(0,0);
//   }
// }


#include<stdio.h>
#include<math.h>
#include<string.h>
int main() {
    float A = 0, B = 0;
    float i, j;
    int k;
    float z[1760];
    char b[1760];
    printf("\x1b[2J");
    for(;;) {
        memset(b,32,1760);
        memset(z,0,7040);
        for(j=0; j < 6.28; j += 0.07) {
            for(i=0; i < 6.28; i += 0.02) {
                float c = sin(i);
                float d = cos(j);
                float e = sin(A);
                float f = sin(j);
                float g = cos(A);
                float h = d + 2;
                float D = 1 / (c * h * e + f * g + 5);
                float l = cos(i);
                float m = cos(B);
                float n = sin(B);
                float t = c * h * g - f * e;
                int x = 40 + 30 * D * (l * h * m - t * n);
                int y= 12 + 15 * D * (l * h * n + t * m);
                int o = x + 80 * y;
                int N = 8 * ((f * e - c * d * g) * m - c * d * e - f * g - l * d * n);
                if(22 > y && y > 0 && x > 0 && 80 > x && D > z[o]) {
                    z[o] = D;
                    b[o] = ".,-~:;=!*#$@"[N > 0 ? N : 0];
                }
            }
        }
        printf("\x1b[H");
        for(k = 0; k < 1761; k++) {
            putchar(k % 80 ? b[k] : 10);
            A += 0.00004;
            B += 0.00002;
        }
        sleep(16);
    }
    return 0;
}
 
void*memset(void*s,int ch,size_t n);
void usleep(int micro_seconds);