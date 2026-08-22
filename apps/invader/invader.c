#include <acl.c>

char fighter0[] =
	"000000000001100000000000"
	"000000000111111000000000"
	"000000011111111110000000"
	"000000011100001110000000"
	"000000011100001110000000"
	"000000011100001110000000"
	"000000011100001110000000"
	"000000011111111110000000"
	"000000011111111110000000"
	"010000111111111111000010"
	"010001111110011111100010"
	"010011111110011111110010"
	"010111111110011111111010"
	"011111111110011111111110"
	"011111111111111111111110"
	"000000000000000000000000";

char invader0[] =
	"00000000000000000000000000000000"
	"00000000000011111111000000000000"
	"00000000011111111111111000000000"
	"01000011111111111111111111000010"
	"01011111110011111111001111111010"
	"01011111110011111111001111111010"
	"01011111110011111111001111111010"
	"01111111111111111111111111111110"
	"00011111111111111111111111111000"
	"00011111111000000000011111111000"
	"00011111111111111111111111111000"
	"00011111111111111111111111111000"
	"00000000110000000000001100000000"
	"00000000110000000000001100000000"
	"00111111110000000000001111111100"
	"00000000000000000000000000000000";

AInt32a fighter[24 * 16], invader[32 * 16];

void initChar(char *s, AInt32a *d, AInt32 c)
{
	AInt16 i;
	for (i = 0; s[i] != 0; i++)
		d[i] = (s[i] - '0') * c;
}

void putChar(AWindow *win, AInt16 x, AInt16 y, AInt32a *p, AInt16 sx)
{
	AInt16 i, j;
	AInt32a *q = win->buf + ((x * 8 + 2) + (y * 16 + 2) * win->xsiz);
	for (j = 0; j < 16; j++) {
		for (i = 0; i < sx; i++)
			q[i] = p[i];
		p += sx;
		q += win->xsiz;
	}
}

void aMain()
{
	AInt32 high = 0, score, point;
	AInt16 ix, iy, i, j, k, lx, ly, lwait, mwait, idir, waitunit, invline, mwait0, fx;
	char inv[6][32], s[64];
	AWindow *win = aOpenWin(324, 228, "invader", 1);
	initChar(fighter0, fighter, 0x00ffff);
	initChar(invader0, invader, 0x00ff00);

restart:
	score = 0;
	point = 1;
	mwait0 = 20;
	fx = 18;

next:
	ix = 7;
	iy = 1;
	invline = 5;
	lx = ly = 0;
	lwait = 0;
	mwait = mwait0;
	idir = +1;
	waitunit = 1024;
	for (i = 0; i < 6; i++) {
		for (j = 0; j < 26; j++)
			inv[i][j] = j % 5;
	}
	for (;;) {
		// display
		aFillRect0(win, 324, 228, 0, 0, 0x1000000);
		sprintf(s, "SCORE:%08d     HIGH:%08d", score, high);
		aDrawStr0(win, 4 * 8 + 2, 0 * 16 + 2, 0xffffff, 0x000000, s);
		putChar(win, fx, 13, fighter, 24);
		for (i = 0; i < 6; i++) {
			for (j = 0; j < 26; j++) {
				if (inv[i][j] == 1)
					putChar(win, ix + j, iy + i, invader, 32);
			}
		}
		if (ly != 0)
			aFillRect0(win, 2, 14, lx * 8 + 5, ly * 16 + 3, 0xffff00);

		// check next stage
		for (;;) {
			j = 0;
			for (i = 0; i < 26; i++)
				j |= inv[invline][i];
			if (j > 0) break;
			invline--;
			if (invline >= 0) continue;
			mwait0 -= mwait0 / 3;
			aWait(1024);
			goto next;
		}

		// wait
		aWait(waitunit);
		waitunit = 40;
		lwait--;

		// input keys
		j = 0;
		do {
			i = aInkey(win, 1);
			if (i == AKEY_LEFT)  j = -1;
			if (i == AKEY_RIGHT) j = +1;
			if (i == ' ' && lwait <= 0) {
				lwait = 15;
				lx = fx + 1;
				ly = 13;
			}
		} while (i > 0);

		// move fighter
		i = fx + j;
		if (0 <= i && i <= 37) fx = i;

		// laser
		if (ly > 0) {
			ly--;
			if (ly == 0) {
				point -= 10;
				if (point < 1)
					point = 1;
			}
		}

		// check hit
		j = lx - ix;
		k = ly - iy;
		if (0 <= k && k <= 5 && 0 <= j && j <= 24) {
			i = inv[k][j];
			if (i > 0) {
				ly = 0;
				j -= i;
				for (i = 0; i < 6; i++)
					inv[k][j + i] = 0;
				score += point;
				point++;
				if (high < score)
					high = score;
			}
		}

		// move invaders
		if (mwait > 0)
			mwait--;
		else {
			mwait = mwait0;
			ix += idir;
			if (ix < 0 || 14 < ix) {
				if (iy + invline == 12) {
					aDrawStr0(win, 15 * 8 + 2, 6 * 16 + 2, 0xff0000, 0x000000, "GAME OVER");
					do {
						aWait(128);
						i = aInkey(win, 1);
					} while (i != AKEY_ENTER);
					goto restart;
				}
				idir *= -1;
				iy++;
				ix += idir;
			}
		}
	}
}
