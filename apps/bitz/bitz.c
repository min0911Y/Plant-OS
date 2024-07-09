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

#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <string.h>
int j = 0;
int tid;
void hello() {
    while(1) {
        int k = MessageLength(tid);
        if(k != -1) {
            char d[6]= {0};
            GetMessage(d,tid);
            if(strcmp(d,"hello") == 0) {
                ++j;
            }
        }
    }
}
int main() {
    tid = NowTaskID();
  int i = AddThread("", hello, (unsigned)malloc(16 * 1024) + 16 * 1024);
  
  printf("tid = %d\n", i);
  while(1) {
    printf("%d\n",j);
    SendMessage(i,(void *)"hello",5);
    goto_xy(0,0);
  }
}