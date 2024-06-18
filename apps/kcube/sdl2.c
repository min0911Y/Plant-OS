#include <SDL.h>

ASTATIC AInt8 aAutoQuit = 1;

typedef struct AWindow_ {
	char title[64];
	AInt16 xsiz, ysiz;
	AInt32a *buf;
	int table_i;
	#if (AKEYBUFSIZ > 0)
		AInt32a keybuf[AKEYBUFSIZ];
		AInt kbw, kbr;
		AInt32 inkeyPrm[4];
	#endif
	#if (!defined(ANOUSE_LEAPFLUSH))
		AInt32 lastFlush;
	#endif
	AInt8 phase, mode, autoClose, reqClose, keyLv, mosLv;

	SDL_Window *win;
	SDL_Renderer *rndr;
	SDL_Texture *txtr;
} AWindow;

SDL_Window *aWinTable_h[AMAXWINDOWS];
AWindow *aWinTable_w[AMAXWINDOWS + 1];

ASTATIC void aInitWin0(AWindow *w, int xsiz, int ysiz, const char *title, int autoClose)
{
	strncpy(w->title, title, 63);
	w->title[63] = '\0';
	w->xsiz = xsiz;
	w->ysiz = ysiz;
	w->buf = aMalloc(xsiz * ysiz * aSizeof (AInt32));
	memset(w->buf, 0, xsiz * ysiz * aSizeof (AInt32));
	#if (AKEYBUFSIZ > 0)
		w->kbw = w->kbr = 0;
	#endif
	#if (!defined(ANOUSE_LEAPFLUSH))
		if (CLOCKS_PER_SEC != 1000)
			w->lastFlush = clock() * 1000 / CLOCKS_PER_SEC;
		else
			w->lastFlush = clock();
	#endif
	w->phase = 0;
	w->mode = 0;
	w->autoClose = autoClose;
	w->reqClose = 0;
	w->keyLv = 1;
	w->mosLv = 0;
	int i;
	for (i = 0; aWinTable_w[i] != 0; i++);
	#if (defined(ADEBUG))
		if (i >= AMAXWINDOWS)
			aErrExit("aInitWin0: too many windows");
	#endif
	w->table_i = i;
	aWinTable_w[i] = w;
	aWinTable_h[i] = 0;
	w->win = 0;
}

ASTATIC void aWait0(AInt32 msec);	// ��������I�ɌĂяo���Ȃ���΂����Ȃ�.

ASTATIC AWindow *aOpenWin(AInt16 x, AInt16 y, const char *t, AInt8 autoClose)
{
	AWindow *w = aMalloc(aSizeof (AWindow));
	aInitWin0(w, x, y, t, autoClose);
    w->win = SDL_CreateWindow(w->title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w->xsiz, w->ysiz, 0);
	w->rndr = SDL_CreateRenderer(w->win, -1, 0);
    w->txtr = SDL_CreateTexture(w->rndr, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w->xsiz, w->ysiz);
	aWinTable_h[w->table_i] = w->win;
	aWait0(100);
	return w;
}

ASTATIC void aFlushAll0(AWindow *w)
{
	AInt32a *p = w->buf;
	char *q;
	int linebyte;
	if (w->phase <= 1 && w->win != 0 && SDL_LockTexture(w->txtr, 0, (void *) &q, &linebyte) == 0) {
		if (linebyte == w->xsiz * aSizeof (AInt32a))
			memcpy(q, p, w->xsiz * w->ysiz * aSizeof (AInt32a));
		else {
			int y;
			for (y = 0; y < w->ysiz; y++) {
				memcpy(q, p, w->xsiz * aSizeof (AInt32a));
				p += w->xsiz;
				q += linebyte;
			}
		}
		SDL_UnlockTexture(w->txtr);
		SDL_RenderCopy(w->rndr, w->txtr, 0, 0);
		SDL_RenderPresent(w->rndr);
	}
	#if (!defined(ANOUSE_LEAPFLUSH))
		if (CLOCKS_PER_SEC != 1000)
			w->lastFlush = clock() * 1000 / CLOCKS_PER_SEC;
		else
			w->lastFlush = clock();
	#endif
}

ASTATIC void aFlushAll(AWindow *w)
{
	aWait0(0);
	if (w != 0 && w->win != 0) {
		aFlushAll0(w);
	} else {
		int i;
		for (i = 0; i < AMAXWINDOWS; i++) {
			if (aWinTable_w[i] != 0 && aWinTable_h[i] != 0)
				aFlushAll0(aWinTable_w[i]);
		}
	}
}

#if (!defined(ANOUSE_LEAPFLUSH))

ASTATIC void aLeapFlushAll0(AWindow *w, AInt32 ms)
{
	AInt32 t;
	if (CLOCKS_PER_SEC != 1000)
		t = clock() * 1000 / CLOCKS_PER_SEC;
	else
		t = clock();
	if (t - w->lastFlush >= ms) {
		aWait0(0);
		aFlushAll0(w);
	}
}

ASTATIC void aLeapFlushAll(AWindow *w, AInt32 ms)
{
	if (w != 0 && w->win != 0) {
		aLeapFlushAll0(w, ms);
	} else {
		int i;
		for (i = 0; i < AMAXWINDOWS; i++) {
			if (aWinTable_w[i] != 0 && aWinTable_h[i] != 0)
				aLeapFlushAll0(aWinTable_w[i], ms);
		}
	}
}

#endif

ASTATIC void aPutKeybuf(AWindow *w, AInt32 c);
ASTATIC AInt aGetSpcKeybuf(AWindow *w);

ASTATIC AWindow *aGetWinFromWId(AUInt32 i)
{
	int j;
	for (j = 0; j < AMAXWINDOWS; j++) {
		if (aWinTable_h[j] != 0 && SDL_GetWindowID(aWinTable_h[j]) == i)
			break;
	}
	if (j >= AMAXWINDOWS)
		return 0;
	return aWinTable_w[j];
}

ASTATIC int aAliveWindows()
{
	int i, c = 0;
	for (i = 0; i < AMAXWINDOWS; i++) {
		AWindow *w = aWinTable_w[i];
		if (w != 0 && w->phase <= 1)
			c++;
	}
	return c;
}

ASTATIC void aWait0(AInt32 msec)
{
	do {
		#if (defined(AOPT_FORCE_LEAPFLUSH))
			{
				static AInt32 t0 = 0;
				AInt32 t;
				if (CLOCKS_PER_SEC != 1000)
					t = clock() * 1000 / CLOCKS_PER_SEC;
				else
					t = clock();
				if (t - t0 >= AOPT_FORCE_LEAPFLUSH) {
					t0 = t;
					aFlushAll(0);
				}
			}
		#endif
		SDL_Event ev;
		while (SDL_PollEvent(&ev) != 0) {
			if (ev.type == SDL_WINDOWEVENT) {
				int i;
				static int tbl[] = {
					SDL_WINDOWEVENT_SHOWN, SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_EXPOSED, SDL_WINDOWEVENT_MOVED,
					SDL_WINDOWEVENT_RESIZED, SDL_WINDOWEVENT_SIZE_CHANGED, SDL_WINDOWEVENT_MINIMIZED,
					SDL_WINDOWEVENT_MAXIMIZED, SDL_WINDOWEVENT_RESTORED, -999
				};
				for (i = 0; tbl[i] != -999; i++) {
					if (ev.window.event == tbl[i]) {
						AWindow *w = aGetWinFromWId(ev.window.windowID);
						if (w != 0 && w->phase <= 1)
							aFlushAll0(w);
					}
				}
				#if (AKEYBUFSIZ > 0 && !defined(ANOUSE_MOUSE))
					if (ev.window.event == SDL_WINDOWEVENT_ENTER) {
						AWindow *w = aGetWinFromWId(ev.window.windowID);
						if (w != 0 && w->mosLv >= 2 && aGetSpcKeybuf(w) >= 2) {
							aPutKeybuf(w, 0x4020);
							aPutKeybuf(w, 0x08000);
						}
					}
					if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
						AWindow *w = aGetWinFromWId(ev.window.windowID);
						if (w != 0 && w->mosLv >= 2 && aGetSpcKeybuf(w) >= 2) {
							aPutKeybuf(w, 0x4020);
							aPutKeybuf(w, 0x18000);
						}
					}
				#endif
				if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
					AWindow *w = aGetWinFromWId(ev.window.windowID);
					if (w != 0) {
						w->reqClose = 1;
						if (w->phase <= 1 && w->autoClose != 0) {
							w->phase = 2;
							SDL_DestroyTexture(w->txtr);
							SDL_DestroyRenderer(w->rndr);
							SDL_DestroyWindow(w->win);
							if (aAutoQuit != 0 && aAliveWindows() == 0)
								aExitInt(0);
						}
					}
				}
			}
#if (AKEYBUFSIZ > 0)
#if (!defined(ANOUSE_KEY))
			if (ev.type == SDL_KEYDOWN) {
				int i = -1;
				SDL_Keycode sym = ev.key.keysym.sym;
				if (sym == SDLK_RETURN)		i = AKEY_ENTER;
				if (sym == SDLK_ESCAPE)		i = AKEY_ESC;
				if (sym == SDLK_BACKSPACE)	i = AKEY_BACKSPACE;
				if (sym == SDLK_TAB)		i = AKEY_TAB;
				if (sym == SDLK_PAGEUP)		i = AKEY_PAGEUP;
				if (sym == SDLK_PAGEDOWN)	i = AKEY_PAGEDWN;
				if (sym == SDLK_END)		i = AKEY_END;
				if (sym == SDLK_HOME)		i = AKEY_HOME;
				if (sym == SDLK_LEFT)		i = AKEY_LEFT;
				if (sym == SDLK_RIGHT)		i = AKEY_RIGHT;
				if (sym == SDLK_UP)			i = AKEY_UP;
				if (sym == SDLK_DOWN)		i = AKEY_DOWN;
				if (sym == SDLK_INSERT)		i = AKEY_INS;
				if (sym == SDLK_DELETE)		i = AKEY_DEL;
				if (0x20 <= sym && sym <= 0x7e)	i = sym;
				if (i >= 0) {
					AWindow *w = aGetWinFromWId(ev.key.windowID);
					if (w != 0 && w->keyLv >= 1)
						aPutKeybuf(w, i);
				}
			}
#endif
#if (!defined(ANOUSE_MOUSE))
			if (ev.type == SDL_MOUSEBUTTONDOWN) {
				// 40xy: x=0(down), x=1(up), x=2(move)
				AWindow *w = aGetWinFromWId(ev.button.windowID);
				if (w != 0 && w->mosLv >= 1 && aGetSpcKeybuf(w) >= 2) {
					int i = -1;
					if (ev.button.button == SDL_BUTTON_LEFT) i = 0;
					if (ev.button.button == SDL_BUTTON_RIGHT) i = 1;
					if (ev.button.button == SDL_BUTTON_MIDDLE) i = 2;
					if (i >= 0) {
						aPutKeybuf(w, 0x4000 | i);
						aPutKeybuf(w, (ev.button.x & 0xffff) | (ev.button.y & 0xffff) << 16);
					}
				}
			}
			if (ev.type == SDL_MOUSEBUTTONUP) {
				AWindow *w = aGetWinFromWId(ev.button.windowID);
				if (w != 0 && w->mosLv >= 2 && aGetSpcKeybuf(w) >= 2) {
					int i = -1;
					if (ev.button.button == SDL_BUTTON_LEFT) i = 0;
					if (ev.button.button == SDL_BUTTON_RIGHT) i = 1;
					if (ev.button.button == SDL_BUTTON_MIDDLE) i = 2;
					if (i >= 0) {
						aPutKeybuf(w, 0x4010 | i);
						aPutKeybuf(w, (ev.button.x & 0xffff) | (ev.button.y & 0xffff) << 16);
					}
				}
			}
			if (ev.type == SDL_MOUSEMOTION) {
				AWindow *w = aGetWinFromWId(ev.motion.windowID);
				if (w != 0 && w->mosLv >= 2 && aGetSpcKeybuf(w) >= 2) {
					aPutKeybuf(w, 0x4020);
					aPutKeybuf(w, (ev.motion.x & 0xffff) | (ev.motion.y & 0xffff) << 16);
				}
			}
#endif
#endif
		}
		if (msec < 0)
			SDL_Delay(64);
		if (msec > 0) {
			if (msec >= 64) {
				SDL_Delay(64);
				msec -= 64;
			} else {
				SDL_Delay(msec);
				msec = 0;
			}
		}
		if (aAutoQuit != 0 && aAliveWindows() == 0)
			aExitInt(0);
	} while (msec != 0);
}
