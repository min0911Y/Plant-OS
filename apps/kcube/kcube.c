#include <acl.c>

void drawObj();
void drawPoly(int j);

double vx[8], vy[8], vz[8], centerz4[6];
AInt32 scx[8], scy[8];
AInt8 squar[24] = { 0,4,6,2, 1,3,7,5, 0,2,3,1, 0,1,5,4, 4,5,7,6, 6,7,3,2 };
AInt32 col[6] = { 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff };
AWindow *win;

void aMain()
{
	win = aOpenWin(256, 160, "kcube", 1);

    static double vertx[8] = {  50.0,  50.0,  50.0,  50.0, -50.0, -50.0, -50.0, -50.0 };
    static double verty[8] = {  50.0,  50.0, -50.0, -50.0,  50.0,  50.0, -50.0, -50.0 };
    static double vertz[8] = {  50.0, -50.0,  50.0, -50.0,  50.0, -50.0,  50.0, -50.0 };
    AInt32 thx = 0, thy = 0, thz = 0, i, l;
    const double toRad = 3.14159265358979323 / 0x8000;
    for (;;) {
        thx = (thx + 182) & 0xffff;
        thy = (thy + 273) & 0xffff;
        thz = (thz + 364) & 0xffff;
        double xp = cos(thx * toRad), xa = sin(thx * toRad);
        double yp = cos(thy * toRad), ya = sin(thy * toRad);
        double zp = cos(thz * toRad), za = sin(thz * toRad);
        for (i = 0; i < 8; i++) {
            double xt, yt, zt;
            zt    = vertz[i] * xp + verty[i] * xa;
            yt    = verty[i] * xp - vertz[i] * xa;
            xt    = vertx[i] * yp + zt       * ya;
            vz[i] = zt       * yp - vertx[i] * ya;
            vx[i] = xt       * zp - yt *       za;
            vy[i] = yt       * zp + xt *       za;
        }
        l = 0; for (i = 0; i < 6; i++) {
            centerz4[i] = vz[squar[l + 0]] + vz[squar[l + 1]]
                        + vz[squar[l + 2]] + vz[squar[l + 3]] + 1024.0;
            l += 4;
        }
        aFillRect0(win, 160, 160, 40, 0, 0x000000);
        drawObj();
        aWait(50);
    }
}

void drawObj()
{
    AInt8 i;
    for (i = 0; i < 8; i++) {
        double t = 300.0 / (vz[i] + 400.0);
        scx[i] = (vx[i] * t) + 128;
        scy[i] = (vy[i] * t) +  80;
    }
    for (;;) {
        double max = 0.0;
        int j = -1, k;
        for (k = 0; k < 6; k++) {
            if (max < centerz4[k]) {
                max = centerz4[k];
                j = k;
            }
        }
        if (j < 0) break;
        i = j * 4;
        centerz4[j] = 0.0;
        double e0x = vx[squar[i + 1]] - vx[squar[i + 0]];
        double e0y = vy[squar[i + 1]] - vy[squar[i + 0]];
        double e1x = vx[squar[i + 2]] - vx[squar[i + 1]];
        double e1y = vy[squar[i + 2]] - vy[squar[i + 1]];
        if (e0x * e1y <= e0y * e1x)
            drawPoly(j);
    }
}

void drawPoly(int j)
{
    int i = j * 4, i1 = i + 3;
    int p0x = scx[squar[i1]], p0y = scy[squar[i1]], p1x, p1y;
    int y, ymin = 0x7fffffff, ymax = 0, x, dx, y0, y1;
    int *buf, buf0[160], buf1[160];
    int c = col[j];
    for (i = i; i <= i1; i++) {
        p1x = scx[squar[i]];
        p1y = scy[squar[i]];
        if (ymin > p1y) ymin = p1y;
        if (ymax < p1y) ymax = p1y;
        if (p0y != p1y) {
            if (p0y < p1y) {
                buf = buf0; y0 = p0y; y1 = p1y; dx = p1x - p0x; x = p0x;
            } else {
                buf = buf1; y0 = p1y; y1 = p0y; dx = p0x - p1x; x = p1x;
            }
            x <<= 16;
            dx = (dx << 16) / (y1 - y0);
            if (dx >= 0)
                x += 0x8000;
            else
                x -= 0x8000;
            for (y = y0; y <= y1; y++) {
                buf[y] = x >> 16;
                x += dx;
            }
        }
        p0x = p1x;
        p0y = p1y;
    }
    for (y = ymin; y <= ymax; y++) {
        p0x = buf0[y];
        p1x = buf1[y];
        if (p0x <= p1x)
            aFillRect0(win, p1x - p0x + 1, 1, p0x, y, c);
        else
            aFillRect0(win, p0x - p1x + 1, 1, p1x, y, c);
    }
}

