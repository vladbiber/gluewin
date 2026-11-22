#ifndef GLUEWIN_COMMON_H
#define GLUEWIN_COMMON_H
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <png.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>

#define S static __attribute__((unused))
#define LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, a, b) MIN(MAX(v, a), b)
#define T(en, ro) (RO ? (ro) : (en))

S Display *dpy;
S int scr, depth;
S Window root;
S Visual *vis;
S Colormap cmap;
S XRenderPictFormat *fmt, *fmt32, *fmt8;
S int RO;
S Atom A_RELOAD;

/* ---------- config ---------- */

typedef struct {
	int dark, align_left, bar_top, tiling;
	unsigned accent;
	char wallpaper[512], pinned[1024], start[2048];
	char terminal[128], files[128];
} Cfg;
S Cfg cfg;

S int have(const char *cmd)
{
	char path[1024], *p, *s, *e = getenv("PATH");
	if (!e) return 0;
	s = strdup(e);
	for (p = strtok(s, ":"); p; p = strtok(NULL, ":")) {
		snprintf(path, sizeof path, "%s/%s", p, cmd);
		if (access(path, X_OK) == 0) { free(s); return 1; }
	}
	free(s);
	return 0;
}

S const char *first_have(const char **list)
{
	for (; *list; list++) if (have(*list)) return *list;
	return NULL;
}

S void cfgfile(char *out, size_t n)
{
	const char *x = getenv("XDG_CONFIG_HOME");
	if (x && *x) snprintf(out, n, "%s/gluewin/config", x);
	else snprintf(out, n, "%s/.config/gluewin/config", getenv("HOME") ? getenv("HOME") : "/tmp");
}

S void cfg_read(const char *path)
{
	char line[2048], *k, *v, *e;
	FILE *f = fopen(path, "r");
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		if (line[0] == '#' || !(v = strchr(line, '='))) continue;
		*v++ = 0;
		for (k = line; isspace((unsigned char)*k); k++);
		for (e = k + strlen(k); e > k && isspace((unsigned char)e[-1]);) *--e = 0;
		while (isspace((unsigned char)*v)) v++;
		v[strcspn(v, "\r\n")] = 0;
		if (!strcmp(k, "theme")) cfg.dark = !strcmp(v, "dark");
		else if (!strcmp(k, "accent")) cfg.accent = (unsigned)strtoul(v + (*v == '#'), NULL, 16);
		else if (!strcmp(k, "align")) cfg.align_left = !strcmp(v, "left");
		else if (!strcmp(k, "bar")) cfg.bar_top = !strcmp(v, "top");
		else if (!strcmp(k, "tiling")) cfg.tiling = atoi(v) > 0;
		else if (!strcmp(k, "wallpaper")) snprintf(cfg.wallpaper, sizeof cfg.wallpaper, "%s", v);
		else if (!strcmp(k, "pinned")) snprintf(cfg.pinned, sizeof cfg.pinned, "%s", v);
		else if (!strcmp(k, "start")) snprintf(cfg.start, sizeof cfg.start, "%s", v);
		else if (!strcmp(k, "terminal")) snprintf(cfg.terminal, sizeof cfg.terminal, "%s", v);
		else if (!strcmp(k, "files")) snprintf(cfg.files, sizeof cfg.files, "%s", v);
	}
	fclose(f);
}

S void cfg_load(void)
{
	char p[512];
	static const char *terms[] = {"alacritty", "kitty", "xfce4-terminal", "st-glue", "xterm", NULL};
	static const char *fms[] = {"thunar", "pcmanfm", "nemo", "nautilus", "dolphin", "caja", NULL};
	const char *t;
	memset(&cfg, 0, sizeof cfg);
	cfg.accent = 0x0067C0;
	strcpy(cfg.pinned, "files.desktop browser.desktop terminal.desktop glue-apps.desktop gluewin-settings.desktop");
	strcpy(cfg.wallpaper, "/usr/share/backgrounds/glue/wallpaper.png");
	cfg_read("/etc/gluewin/config");
	cfgfile(p, sizeof p);
	cfg_read(p);
	if (!cfg.terminal[0] && (t = first_have(terms))) snprintf(cfg.terminal, sizeof cfg.terminal, "%s", t);
	if (!cfg.files[0]) {
		if (have("glue-action")) strcpy(cfg.files, "glue-action files");
		else if ((t = first_have(fms))) snprintf(cfg.files, sizeof cfg.files, "%s", t);
		else strcpy(cfg.files, "xdg-open ~");
	}
}

S void cfg_save(void)
{
	char p[512], d[512], tmp[520];
	FILE *f;
	cfgfile(p, sizeof p);
	snprintf(d, sizeof d, "%s", p);
	*strrchr(d, '/') = 0;
	*strrchr(d, '/') = 0;
	mkdir(d, 0755);
	snprintf(d, sizeof d, "%s", p);
	*strrchr(d, '/') = 0;
	mkdir(d, 0755);
	snprintf(tmp, sizeof tmp, "%s.tmp", p);
	if (!(f = fopen(tmp, "w"))) return;
	fprintf(f, "theme=%s\naccent=#%06X\nalign=%s\nbar=%s\ntiling=%d\nwallpaper=%s\npinned=%s\nstart=%s\nterminal=%s\nfiles=%s\n",
		cfg.dark ? "dark" : "light", cfg.accent, cfg.align_left ? "left" : "center",
		cfg.bar_top ? "top" : "bottom", cfg.tiling,
		cfg.wallpaper, cfg.pinned, cfg.start, cfg.terminal, cfg.files);
	if (fclose(f) == 0) rename(tmp, p);
}

S void send_reload(void)
{
	XEvent ev = {0};
	ev.xclient.type = ClientMessage;
	ev.xclient.window = root;
	ev.xclient.message_type = A_RELOAD;
	ev.xclient.format = 32;
	XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
}

/* ---------- theme ---------- */

typedef struct {
	unsigned bar, line, menu, foot, card, border, text, sub, hover, press;
	unsigned title, title_in, accent, onaccent, desk;
} Theme;
S Theme th;

S unsigned mix(unsigned a, unsigned b, int t)
{
	unsigned r = 0;
	for (int s = 0; s < 24; s += 8) {
		int x = (a >> s) & 255, y = (b >> s) & 255;
		r |= (unsigned)(x + (y - x) * t / 255) << s;
	}
	return r;
}

S void theme_apply(void)
{
	if (cfg.dark) {
		th = (Theme){0x1C1C1C, 0x2E2E2E, 0x242424, 0x1E1E1E, 0x2D2D2D, 0x3A3A3A, 0xFFFFFF,
			0xBDBDBD, 0x333333, 0x3D3D3D, 0x202020, 0x2B2B2B, 0, 0, 0x0B1A33};
		th.accent = mix(cfg.accent, 0xFFFFFF, 110);
		th.onaccent = 0x000000;
	} else {
		th = (Theme){0xF0F3F9, 0xDADDE3, 0xF3F3F3, 0xE9EBEF, 0xFFFFFF, 0xDDDDDD, 0x1A1A1A,
			0x5D5D5D, 0xE2E4E8, 0xD6D8DC, 0xEFF4F9, 0xF9F9F9, 0, 0, 0x6E8FC9};
		th.accent = cfg.accent;
		th.onaccent = 0xFFFFFF;
	}
}

/* ---------- drawing ---------- */

typedef struct { Pixmap pm; Picture pic; XftDraw *xd; int w, h; } Buf;
S Buf B;
S XftFont *F_sm, *F_ui, *F_md, *F_lg, *F_xl;

S void gfx_init(void)
{
	scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	vis = DefaultVisual(dpy, scr);
	cmap = DefaultColormap(dpy, scr);
	depth = DefaultDepth(dpy, scr);
	fmt = XRenderFindVisualFormat(dpy, vis);
	fmt32 = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	fmt8 = XRenderFindStandardFormat(dpy, PictStandardA8);
	A_RELOAD = XInternAtom(dpy, "_GLUEWIN_RELOAD", False);
	const char *l = getenv("LC_ALL");
	if (!l || !*l) l = getenv("LC_MESSAGES");
	if (!l || !*l) l = getenv("LANG");
	RO = l && !strncmp(l, "ro", 2);
}

S XftFont *font(const char *size)
{
	char pat[160];
	XftFont *f;
	snprintf(pat, sizeof pat, "Inter,Noto Sans,DejaVu Sans:%s", size);
	f = XftFontOpenName(dpy, scr, pat);
	return f ? f : XftFontOpenName(dpy, scr, "sans-10");
}

S void fonts_init(void)
{
	F_sm = font("pixelsize=12");
	F_ui = font("pixelsize=14");
	F_md = font("pixelsize=14:weight=demibold");
	F_lg = font("pixelsize=20:weight=demibold");
	F_xl = font("pixelsize=28:weight=demibold");
}

S void buf_ensure(int w, int h)
{
	if (B.pm && B.w >= w && B.h >= h) return;
	if (B.pm) { XftDrawDestroy(B.xd); XRenderFreePicture(dpy, B.pic); XFreePixmap(dpy, B.pm); }
	B.w = MAX(w, B.w); B.h = MAX(h, B.h);
	B.pm = XCreatePixmap(dpy, root, B.w, B.h, depth);
	B.pic = XRenderCreatePicture(dpy, B.pm, fmt, 0, NULL);
	B.xd = XftDrawCreate(dpy, B.pm, vis, cmap);
}

S void blit(Window w, int ww, int hh)
{
	XCopyArea(dpy, B.pm, w, DefaultGC(dpy, scr), 0, 0, ww, hh, 0, 0);
}

S XRenderColor rcol(unsigned rgb, int a)
{
	XRenderColor c;
	c.alpha = a * 257;
	c.red = ((rgb >> 16) & 255) * a / 255 * 257;
	c.green = ((rgb >> 8) & 255) * a / 255 * 257;
	c.blue = (rgb & 255) * a / 255 * 257;
	return c;
}

S void fill(int x, int y, int w, int h, unsigned rgb)
{
	XRenderColor c = rcol(rgb, 255);
	if (w > 0 && h > 0) XRenderFillRectangle(dpy, PictOpSrc, B.pic, &c, x, y, w, h);
}

S Picture circmask(int r)
{
	static Picture cache[65];
	int d, i, j, si, sj, n;
	char *px;
	Pixmap pm;
	GC gc;
	XImage *im;
	r = CLAMP(r, 1, 64);
	if (cache[r]) return cache[r];
	d = 2 * r;
	px = calloc((size_t)d, (size_t)d);
	for (j = 0; j < d; j++)
		for (i = 0; i < d; i++) {
			for (n = 0, sj = 0; sj < 4; sj++)
				for (si = 0; si < 4; si++) {
					float x = i + (si + .5f) / 4 - r, y = j + (sj + .5f) / 4 - r;
					n += x * x + y * y <= (float)r * r;
				}
			px[j * d + i] = (char)(n * 255 / 16);
		}
	pm = XCreatePixmap(dpy, root, d, d, 8);
	im = XCreateImage(dpy, vis, 8, ZPixmap, 0, px, d, d, 8, d);
	gc = XCreateGC(dpy, pm, 0, NULL);
	XPutImage(dpy, pm, gc, im, 0, 0, 0, 0, d, d);
	XFreeGC(dpy, gc);
	XDestroyImage(im);
	cache[r] = XRenderCreatePicture(dpy, pm, fmt8, 0, NULL);
	XFreePixmap(dpy, pm);
	return cache[r];
}

S void rrect(int x, int y, int w, int h, int r, unsigned rgb, int a)
{
	XRenderColor c = rcol(rgb, a);
	Picture src, m;
	if (w <= 0 || h <= 0) return;
	r = MIN(r, MIN(w, h) / 2);
	if (r <= 0) { XRenderFillRectangle(dpy, PictOpOver, B.pic, &c, x, y, w, h); return; }
	XRenderFillRectangle(dpy, PictOpOver, B.pic, &c, x + r, y, w - 2 * r, h);
	XRenderFillRectangle(dpy, PictOpOver, B.pic, &c, x, y + r, r, h - 2 * r);
	XRenderFillRectangle(dpy, PictOpOver, B.pic, &c, x + w - r, y + r, r, h - 2 * r);
	src = XRenderCreateSolidFill(dpy, &c);
	m = circmask(r);
	XRenderComposite(dpy, PictOpOver, src, m, B.pic, 0, 0, 0, 0, x, y, r, r);
	XRenderComposite(dpy, PictOpOver, src, m, B.pic, 0, 0, r, 0, x + w - r, y, r, r);
	XRenderComposite(dpy, PictOpOver, src, m, B.pic, 0, 0, 0, r, x, y + h - r, r, r);
	XRenderComposite(dpy, PictOpOver, src, m, B.pic, 0, 0, r, r, x + w - r, y + h - r, r, r);
	XRenderFreePicture(dpy, src);
}

S void rbox(int x, int y, int w, int h, int r, unsigned bg, unsigned border)
{
	rrect(x, y, w, h, r, border, 255);
	rrect(x + 1, y + 1, w - 2, h - 2, MAX(r - 1, 0), bg, 255);
}

S XftColor *xcol(unsigned rgb)
{
	static struct { unsigned rgb; XftColor c; int ok; } cache[48];
	static int next;
	XRenderColor rc;
	for (int i = 0; i < (int)LEN(cache); i++)
		if (cache[i].ok && cache[i].rgb == rgb) return &cache[i].c;
	if (cache[next].ok) XftColorFree(dpy, vis, cmap, &cache[next].c);
	rc = rcol(rgb, 255);
	XftColorAllocValue(dpy, vis, cmap, &rc, &cache[next].c);
	cache[next].rgb = rgb;
	cache[next].ok = 1;
	XftColor *r = &cache[next].c;
	next = (next + 1) % (int)LEN(cache);
	return r;
}

S int tw(XftFont *f, const char *s)
{
	XGlyphInfo g;
	XftTextExtentsUtf8(dpy, f, (const FcChar8 *)s, (int)strlen(s), &g);
	return g.xOff;
}

S void text(XftFont *f, int x, int y, const char *s, unsigned rgb)
{
	XftDrawStringUtf8(B.xd, xcol(rgb), f, x, y + f->ascent, (const FcChar8 *)s, (int)strlen(s));
}

/* draws s cut to maxw with an ellipsis; y is the top of the line */
S void textfit(XftFont *f, int x, int y, int maxw, const char *s, unsigned rgb)
{
	char b[512];
	size_t n;
	if (tw(f, s) <= maxw) { text(f, x, y, s, rgb); return; }
	snprintf(b, sizeof b - 4, "%s", s);
	for (n = strlen(b); n > 0;) {
		do n--; while (n > 0 && (b[n] & 0xC0) == 0x80);
		strcpy(b + n, "…");
		if (tw(f, b) <= maxw) break;
	}
	text(f, x, y, b, rgb);
}

S void textc(XftFont *f, int x, int y, int w, const char *s, unsigned rgb)
{
	int t = tw(f, s);
	if (t > w) textfit(f, x, y, w, s, rgb);
	else text(f, x + (w - t) / 2, y, s, rgb);
}

S int fh(XftFont *f) { return f->ascent + f->descent; }

/* ---------- vector glyphs (anti-aliased, drawn on a 24-unit grid) ---------- */

enum { P_CIRC, P_RING, P_SEG, P_RECT, P_RRO, P_RRF, P_TRI };
typedef struct { int t; float a, b, c, d, e, f; } Prim;
enum {
	G_START, G_SEARCH, G_WIFI, G_WIFIOFF, G_ETH, G_VOL, G_MUTE, G_BATT, G_POWER, G_GEAR,
	G_MIN, G_MAX, G_RESTORE, G_CLOSE, G_CHEVR, G_CHEVL, G_BT, G_SUN, G_LOCK, G_SIGNOUT,
	G_RESTART, G_MOON, G_FOLDER, G_APPS, G_BRUSH, G_INFO, G_UPDATE, G_SYSTEM, G_CHECK,
	G_CHEVD, G_PLUS, G_TASKVIEW, NGLYPH
};

S int glyph_prims(int id, Prim *p)
{
	int n = 0;
#define P(...) (p[n++] = (Prim){__VA_ARGS__})
	switch (id) {
	case G_START:
		P(P_RRF, 3, 3, 8.2f, 8.2f, 2, 0); P(P_RRF, 12.8f, 3, 8.2f, 8.2f, 2, 0);
		P(P_RRF, 3, 12.8f, 8.2f, 8.2f, 2, 0); P(P_CIRC, 16.9f, 16.9f, 4.3f);
		break;
	case G_SEARCH: P(P_RING, 10, 10, 6, 2, 0, 0); P(P_SEG, 14.5f, 14.5f, 20, 20, 2.2f); break;
	case G_WIFIOFF:
	case G_WIFI:
		P(P_RING, 12, 19, 5, 1.8f, 45, 135); P(P_RING, 12, 19, 9.5f, 1.8f, 45, 135);
		P(P_RING, 12, 19, 14, 1.8f, 45, 135); P(P_CIRC, 12, 19, 1.7f);
		if (id == G_WIFIOFF) P(P_SEG, 4, 4, 20, 20, 1.8f);
		break;
	case G_ETH: P(P_RRO, 3, 4, 18, 12.5f, 2, 1.7f); P(P_SEG, 12, 16.5f, 12, 20, 1.7f); P(P_SEG, 7, 20, 17, 20, 1.7f); break;
	case G_SYSTEM: P(P_RRO, 3, 4, 18, 12.5f, 2, 1.7f); P(P_SEG, 12, 16.5f, 12, 20, 1.7f); P(P_SEG, 7, 20, 17, 20, 1.7f); break;
	case G_MUTE:
	case G_VOL:
		P(P_RECT, 3, 9, 4, 6); P(P_TRI, 6.5f, 9, 12, 4, 12, 20); P(P_TRI, 6.5f, 9, 12, 20, 6.5f, 15);
		if (id == G_VOL) { P(P_RING, 12, 12, 4.2f, 1.6f, 310, 50); P(P_RING, 12, 12, 8, 1.6f, 310, 50); }
		else { P(P_SEG, 15, 9, 21, 15, 1.6f); P(P_SEG, 21, 9, 15, 15, 1.6f); }
		break;
	case G_BATT: P(P_RRO, 2, 7, 18, 10, 2.5f, 1.6f); P(P_RRF, 20.3f, 10, 2, 4, .8f, 0); break;
	case G_POWER: P(P_RING, 12, 13, 7.5f, 2, 125, 55); P(P_SEG, 12, 3.5f, 12, 12, 2); break;
	case G_GEAR:
		P(P_RING, 12, 12, 6.2f, 2.6f, 0, 0);
		for (int i = 0; i < 8; i++) {
			float a = (float)(i * M_PI / 4);
			P(P_SEG, 12 + 7.2f * cosf(a), 12 + 7.2f * sinf(a), 12 + 9.4f * cosf(a), 12 + 9.4f * sinf(a), 3.2f);
		}
		break;
	case G_MIN: P(P_SEG, 7, 12, 17, 12, 1.1f); break;
	case G_MAX: P(P_RRO, 7, 7, 10, 10, 1.6f, 1.1f); break;
	case G_RESTORE:
		P(P_RRO, 7, 9, 8, 8, 1.4f, 1.1f); P(P_SEG, 9.5f, 7, 15.5f, 7, 1.1f);
		P(P_SEG, 17, 8.5f, 17, 14.5f, 1.1f); P(P_RING, 15.5f, 8.5f, 1.5f, 1.1f, 0, 90);
		break;
	case G_CLOSE: P(P_SEG, 7, 7, 17, 17, 1.1f); P(P_SEG, 17, 7, 7, 17, 1.1f); break;
	case G_CHEVR: P(P_SEG, 9.5f, 6, 15.5f, 12, 1.8f); P(P_SEG, 15.5f, 12, 9.5f, 18, 1.8f); break;
	case G_CHEVL: P(P_SEG, 14.5f, 6, 8.5f, 12, 1.8f); P(P_SEG, 8.5f, 12, 14.5f, 18, 1.8f); break;
	case G_CHEVD: P(P_SEG, 6, 9.5f, 12, 15.5f, 1.8f); P(P_SEG, 12, 15.5f, 18, 9.5f, 1.8f); break;
	case G_BT:
		P(P_SEG, 7, 8, 16.5f, 16, 1.7f); P(P_SEG, 16.5f, 16, 12, 20.5f, 1.7f); P(P_SEG, 12, 20.5f, 12, 3.5f, 1.7f);
		P(P_SEG, 12, 3.5f, 16.5f, 8, 1.7f); P(P_SEG, 16.5f, 8, 7, 16, 1.7f);
		break;
	case G_SUN:
		P(P_CIRC, 12, 12, 4);
		for (int i = 0; i < 8; i++) {
			float a = (float)(i * M_PI / 4);
			P(P_SEG, 12 + 6.8f * cosf(a), 12 + 6.8f * sinf(a), 12 + 9.2f * cosf(a), 12 + 9.2f * sinf(a), 1.8f);
		}
		break;
	case G_MOON: P(P_RING, 12, 12, 6.5f, 3.2f, 70, 290); P(P_RING, 13.5f, 12, 3.2f, 2.4f, 100, 260); break;
	case G_LOCK: P(P_RRF, 5, 11, 14, 10, 2, 0); P(P_RING, 12, 10, 4, 2, 0, 180); P(P_SEG, 8, 10, 8, 11.5f, 2); P(P_SEG, 16, 10, 16, 11.5f, 2); break;
	case G_SIGNOUT:
		P(P_SEG, 10, 12, 20, 12, 1.8f); P(P_SEG, 16.5f, 8.5f, 20, 12, 1.8f); P(P_SEG, 16.5f, 15.5f, 20, 12, 1.8f);
		P(P_SEG, 4.5f, 4.5f, 4.5f, 19.5f, 1.8f); P(P_SEG, 4.5f, 4.5f, 11, 4.5f, 1.8f); P(P_SEG, 4.5f, 19.5f, 11, 19.5f, 1.8f);
		break;
	case G_UPDATE:
	case G_RESTART:
		P(P_RING, 12, 12, 7.5f, 2, 120, 60); P(P_TRI, 13, 1.5f, 13, 8.5f, 18, 5);
		if (id == G_UPDATE) P(P_CIRC, 12, 12, 2);
		break;
	case G_FOLDER: P(P_RRF, 3, 7, 18, 13, 2, 0); P(P_RRF, 3, 5, 8, 5, 1.5f, 0); break;
	case G_APPS:
		P(P_RRO, 3.5f, 3.5f, 7.5f, 7.5f, 1.8f, 1.6f); P(P_RRO, 13, 3.5f, 7.5f, 7.5f, 1.8f, 1.6f);
		P(P_RRO, 3.5f, 13, 7.5f, 7.5f, 1.8f, 1.6f); P(P_RRO, 13, 13, 7.5f, 7.5f, 1.8f, 1.6f);
		break;
	case G_BRUSH: P(P_SEG, 5, 19, 14, 10, 3); P(P_SEG, 14, 10, 19, 5, 1.6f); P(P_CIRC, 5.5f, 18.5f, 2.4f); break;
	case G_INFO: P(P_RING, 12, 12, 9, 1.7f, 0, 0); P(P_SEG, 12, 11, 12, 16.5f, 2); P(P_CIRC, 12, 7.6f, 1.3f); break;
	case G_CHECK: P(P_SEG, 5, 12.5f, 10, 17.5f, 2); P(P_SEG, 10, 17.5f, 19.5f, 7, 2); break;
	case G_PLUS: P(P_SEG, 12, 5, 12, 19, 1.8f); P(P_SEG, 5, 12, 19, 12, 1.8f); break;
	case G_TASKVIEW:
		P(P_RRO, 8.5f, 3.5f, 12, 12, 2, 1.6f);
		P(P_SEG, 3.8f, 9.5f, 3.8f, 19.5f, 1.6f); P(P_SEG, 4.5f, 20.2f, 14.5f, 20.2f, 1.6f);
		P(P_SEG, 4.5f, 8.8f, 7, 8.8f, 1.6f); P(P_SEG, 15.2f, 17, 15.2f, 19.5f, 1.6f);
		break;
	}
#undef P
	return n;
}

S float rr_sdf(float px, float py, float x, float y, float w, float h, float r)
{
	float qx = fabsf(px - (x + w / 2)) - w / 2 + r, qy = fabsf(py - (y + h / 2)) - h / 2 + r;
	float ox = MAX(qx, 0), oy = MAX(qy, 0);
	return sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0) - r;
}

S int prim_in(const Prim *p, float x, float y)
{
	float dx, dy, d, l, t, ang;
	switch (p->t) {
	case P_CIRC: dx = x - p->a; dy = y - p->b; return dx * dx + dy * dy <= p->c * p->c;
	case P_RING:
		dx = x - p->a; dy = y - p->b; d = sqrtf(dx * dx + dy * dy);
		if (fabsf(d - p->c) > p->d / 2) return 0;
		if (p->e == p->f) return 1;
		ang = atan2f(-dy, dx) * 180 / (float)M_PI;
		if (ang < 0) ang += 360;
		return p->e <= p->f ? ang >= p->e && ang <= p->f : ang >= p->e || ang <= p->f;
	case P_SEG:
		dx = p->c - p->a; dy = p->d - p->b; l = dx * dx + dy * dy;
		t = l > 0 ? ((x - p->a) * dx + (y - p->b) * dy) / l : 0;
		t = CLAMP(t, 0, 1);
		dx = x - (p->a + t * dx); dy = y - (p->b + t * dy);
		return dx * dx + dy * dy <= p->e * p->e / 4;
	case P_RECT: return x >= p->a && x < p->a + p->c && y >= p->b && y < p->b + p->d;
	case P_RRF: return rr_sdf(x, y, p->a, p->b, p->c, p->d, p->e) <= 0;
	case P_RRO: d = rr_sdf(x, y, p->a, p->b, p->c, p->d, p->e); return d <= 0 && d > -p->f;
	case P_TRI: {
		float d1 = (x - p->c) * (p->b - p->d) - (p->a - p->c) * (y - p->d);
		float d2 = (x - p->e) * (p->d - p->f) - (p->c - p->e) * (y - p->f);
		float d3 = (x - p->a) * (p->f - p->b) - (p->e - p->a) * (y - p->b);
		return !(((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0)));
	}
	}
	return 0;
}

S Picture glyph_mask(int id, int size)
{
	static struct { int id, size; Picture p; } cache[96];
	static int ncache;
	Prim pr[32];
	int n, i, j, si, sj, k, c;
	char *px;
	Pixmap pm;
	GC gc;
	XImage *im;
	for (i = 0; i < ncache; i++)
		if (cache[i].id == id && cache[i].size == size) return cache[i].p;
	n = glyph_prims(id, pr);
	px = calloc((size_t)size, (size_t)size);
	for (j = 0; j < size; j++)
		for (i = 0; i < size; i++) {
			for (c = 0, sj = 0; sj < 4; sj++)
				for (si = 0; si < 4; si++) {
					float x = (i + (si + .5f) / 4) * 24 / size, y = (j + (sj + .5f) / 4) * 24 / size;
					for (k = 0; k < n; k++) if (prim_in(&pr[k], x, y)) { c++; break; }
				}
			px[j * size + i] = (char)(c * 255 / 16);
		}
	pm = XCreatePixmap(dpy, root, size, size, 8);
	im = XCreateImage(dpy, vis, 8, ZPixmap, 0, px, size, size, 8, size);
	gc = XCreateGC(dpy, pm, 0, NULL);
	XPutImage(dpy, pm, gc, im, 0, 0, 0, 0, size, size);
	XFreeGC(dpy, gc);
	XDestroyImage(im);
	Picture p = XRenderCreatePicture(dpy, pm, fmt8, 0, NULL);
	XFreePixmap(dpy, pm);
	if (ncache < (int)LEN(cache)) cache[ncache++] = (typeof(cache[0])){id, size, p};
	return p;
}

S void glyph(int id, int x, int y, int size, unsigned rgb)
{
	XRenderColor c = rcol(rgb, 255);
	Picture src = XRenderCreateSolidFill(dpy, &c);
	XRenderComposite(dpy, PictOpOver, src, glyph_mask(id, size), B.pic, 0, 0, 0, 0, x, y, size, size);
	XRenderFreePicture(dpy, src);
}

/* ---------- icons ---------- */

typedef struct { unsigned *px; int w, h; Picture pic[3]; int sz[3]; } Icon;

S void icon_free(Icon *ic)
{
	for (int i = 0; i < 3; i++) if (ic->pic[i]) XRenderFreePicture(dpy, ic->pic[i]);
	free(ic->px);
	memset(ic, 0, sizeof *ic);
}

/* takes straight-alpha ARGB, keeps a premultiplied copy no larger than 64px */
S void icon_set(Icon *ic, const unsigned *src, int w, int h)
{
	int dw = w, dh = h, x, y, sx, sy;
	icon_free(ic);
	if (w > 64 || h > 64) { dw = w >= h ? 64 : 64 * w / h; dh = w >= h ? 64 * h / w : 64; }
	dw = MAX(dw, 1); dh = MAX(dh, 1);
	ic->px = malloc((size_t)dw * dh * 4);
	ic->w = dw; ic->h = dh;
	for (y = 0; y < dh; y++)
		for (x = 0; x < dw; x++) {
			int x0 = x * w / dw, x1 = MAX((x + 1) * w / dw, x0 + 1);
			int y0 = y * h / dh, y1 = MAX((y + 1) * h / dh, y0 + 1);
			unsigned long a = 0, r = 0, g = 0, b = 0, n = 0;
			for (sy = y0; sy < y1; sy++)
				for (sx = x0; sx < x1; sx++) {
					unsigned p = src[sy * w + sx], pa = p >> 24;
					a += pa; r += ((p >> 16) & 255) * pa / 255;
					g += ((p >> 8) & 255) * pa / 255; b += (p & 255) * pa / 255; n++;
				}
			ic->px[y * dw + x] = (unsigned)((a / n) << 24 | (r / n) << 16 | (g / n) << 8 | (b / n));
		}
}

S Picture icon_pic(Icon *ic, int size)
{
	int i, slot = 0, x, y;
	unsigned *d;
	for (i = 0; i < 3; i++) if (ic->pic[i] && ic->sz[i] == size) return ic->pic[i];
	for (i = 0; i < 3; i++) if (!ic->pic[i]) { slot = i; break; }
	if (ic->pic[slot]) XRenderFreePicture(dpy, ic->pic[slot]);
	d = malloc((size_t)size * size * 4);
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++) {
			int x0 = x * ic->w / size, x1 = MAX((x + 1) * ic->w / size, x0 + 1);
			int y0 = y * ic->h / size, y1 = MAX((y + 1) * ic->h / size, y0 + 1);
			unsigned long c[4] = {0}, n = 0;
			for (int sy = y0; sy < y1; sy++)
				for (int sx = x0; sx < x1; sx++, n++)
					for (int k = 0; k < 4; k++) c[k] += (ic->px[sy * ic->w + sx] >> (k * 8)) & 255;
			d[y * size + x] = (unsigned)((c[3] / n) << 24 | (c[2] / n) << 16 | (c[1] / n) << 8 | (c[0] / n));
		}
	Pixmap pm = XCreatePixmap(dpy, root, size, size, 32);
	XImage *im = XCreateImage(dpy, vis, 32, ZPixmap, 0, (char *)d, size, size, 32, size * 4);
	GC gc = XCreateGC(dpy, pm, 0, NULL);
	XPutImage(dpy, pm, gc, im, 0, 0, 0, 0, size, size);
	XFreeGC(dpy, gc);
	XDestroyImage(im);
	ic->pic[slot] = XRenderCreatePicture(dpy, pm, fmt32, 0, NULL);
	ic->sz[slot] = size;
	XFreePixmap(dpy, pm);
	return ic->pic[slot];
}

S void icon_draw(Icon *ic, int x, int y, int size)
{
	XRenderComposite(dpy, PictOpOver, icon_pic(ic, size), None, B.pic, 0, 0, 0, 0, x, y, size, size);
}

/* rounded accent tile with the first letter, for apps with no usable icon */
S void letter_tile(const char *name, int x, int y, int size)
{
	char l[8] = "?";
	int n = 1;
	if (name && *name) {
		while (name[n] && (name[n] & 0xC0) == 0x80) n++;
		snprintf(l, sizeof l, "%.*s", n, name);
		if (n == 1) l[0] = (char)toupper((unsigned char)l[0]);
	}
	rrect(x + size / 12, y + size / 12, size - size / 6, size - size / 6, size / 5, th.accent, 255);
	XftFont *f = size >= 28 ? F_lg : F_md;
	textc(f, x, y + (size - fh(f)) / 2, size, l, th.onaccent);
}

S int png_load(const char *path, Icon *ic)
{
	png_image im;
	unsigned *buf;
	memset(&im, 0, sizeof im);
	im.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_file(&im, path)) return 0;
	im.format = PNG_FORMAT_BGRA;
	if (im.width > 1024 || im.height > 1024) { png_image_free(&im); return 0; }
	buf = malloc(PNG_IMAGE_SIZE(im));
	if (!png_image_finish_read(&im, NULL, buf, 0, NULL)) { free(buf); return 0; }
	icon_set(ic, buf, (int)im.width, (int)im.height);
	free(buf);
	return 1;
}

/* ---------- desktop entries ---------- */

typedef struct {
	char id[128], name[128], exec[512], icon[192], wmclass[128], search[640];
	int term, iconst;
	Icon ic;
} App;
S App *apps;
S int napps;

S const char *home(void) { return getenv("HOME") ? getenv("HOME") : "/"; }

S void icon_cache_path(const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s/.cache/gluewin/icons/%s.png", home(), name);
}

/* finds a png (ret 1) or an svg (ret 2) for an icon name */
S int find_icon(const char *name, char *out, size_t n)
{
	static const char *sizes[] = {"48x48", "64x64", "32x32", "128x128", "96x96", "256x256", "24x24", "scalable"};
	static const char *themes[] = {"hicolor", "Papirus", "Adwaita", "breeze"};
	char bases[4][256], svg[512] = "";
	struct stat st;
	int b, t, s;
	if (!name || !*name) return 0;
	if (name[0] == '/') {
		snprintf(out, n, "%s", name);
		if (stat(out, &st)) return 0;
		return strstr(name, ".svg") ? 2 : 1;
	}
	icon_cache_path(name, out, n);
	if (!stat(out, &st)) return 1;
	snprintf(bases[0], 256, "%s/.local/share/icons", home());
	snprintf(bases[1], 256, "/usr/share/icons");
	snprintf(bases[2], 256, "/var/lib/flatpak/exports/share/icons");
	snprintf(bases[3], 256, "%s/.local/share/flatpak/exports/share/icons", home());
	for (t = 0; t < (int)LEN(themes); t++)
		for (b = 0; b < 4; b++)
			for (s = 0; s < (int)LEN(sizes); s++) {
				snprintf(out, n, "%s/%s/%s/apps/%s.png", bases[b], themes[t], sizes[s], name);
				if (!stat(out, &st)) return 1;
				if (!svg[0]) {
					snprintf(out, n, "%s/%s/%s/apps/%s.svg", bases[b], themes[t], sizes[s], name);
					if (!stat(out, &st)) snprintf(svg, sizeof svg, "%s", out);
				}
			}
	snprintf(out, n, "/usr/share/pixmaps/%s.png", name);
	if (!stat(out, &st)) return 1;
	if (!svg[0]) {
		snprintf(out, n, "/usr/share/pixmaps/%s.svg", name);
		if (!stat(out, &st)) snprintf(svg, sizeof svg, "%s", out);
	}
	if (svg[0]) { snprintf(out, n, "%s", svg); return 2; }
	return 0;
}

S int app_icon(App *a)
{
	char p[640];
	if (a->iconst) return a->iconst > 0;
	a->iconst = -1;
	if (find_icon(a->icon, p, sizeof p) == 1 && png_load(p, &a->ic)) a->iconst = 1;
	return a->iconst > 0;
}

/* converts svg-only icons into the png cache; run in a child, it takes a while */
S void icon_cache_svgs(void)
{
	char p[640], out[640], cmd[1600], d[512];
	if (!have("rsvg-convert")) return;
	snprintf(d, sizeof d, "%s/.cache/gluewin/icons", home());
	snprintf(cmd, sizeof cmd, "mkdir -p '%s'", d);
	if (system(cmd)) return;
	for (int i = 0; i < napps; i++) {
		if (find_icon(apps[i].icon, p, sizeof p) != 2 || strchr(p, '\'') || strchr(apps[i].icon, '/')) continue;
		icon_cache_path(apps[i].icon, out, sizeof out);
		snprintf(cmd, sizeof cmd, "rsvg-convert -w 48 -h 48 '%s' -o '%s' 2>/dev/null", p, out);
		if (system(cmd)) unlink(out);
	}
}

S int app_cmp(const void *a, const void *b)
{
	return strcasecmp(((const App *)a)->name, ((const App *)b)->name);
}

S void app_parse(const char *path, const char *id)
{
	char line[1024], *v, lname[32];
	int sect = 0, hidden = 0, isapp = 0, haslocal = 0;
	App a;
	FILE *f;
	for (int i = 0; i < napps; i++) if (!strcmp(apps[i].id, id)) return;
	if (!(f = fopen(path, "r"))) return;
	memset(&a, 0, sizeof a);
	snprintf(a.id, sizeof a.id, "%s", id);
	snprintf(lname, sizeof lname, "Name[%s]", RO ? "ro" : "en");
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == '[') { sect = !strcmp(line, "[Desktop Entry]"); continue; }
		if (!sect || !(v = strchr(line, '='))) continue;
		*v++ = 0;
		if (!strcmp(line, "Name") && !haslocal) snprintf(a.name, sizeof a.name, "%s", v);
		else if (!strcmp(line, lname)) { snprintf(a.name, sizeof a.name, "%s", v); haslocal = 1; }
		else if (!strcmp(line, "Exec")) snprintf(a.exec, sizeof a.exec, "%s", v);
		else if (!strcmp(line, "Icon")) snprintf(a.icon, sizeof a.icon, "%s", v);
		else if (!strcmp(line, "StartupWMClass")) snprintf(a.wmclass, sizeof a.wmclass, "%s", v);
		else if (!strcmp(line, "Terminal")) a.term = !strcmp(v, "true");
		else if (!strcmp(line, "Type")) isapp = !strcmp(v, "Application");
		else if ((!strcmp(line, "NoDisplay") || !strcmp(line, "Hidden")) && !strcmp(v, "true")) hidden = 1;
		else if (!strcmp(line, "GenericName") || !strcmp(line, "Keywords") ||
			(RO && (!strcmp(line, "GenericName[ro]") || !strcmp(line, "Keywords[ro]")))) {
			size_t l = strlen(a.search);
			snprintf(a.search + l, sizeof a.search - l, " %s", v);
		}
	}
	fclose(f);
	if (!isapp || hidden || !a.name[0] || !a.exec[0]) {
		if (!isapp || !a.name[0]) return;
		/* hidden entries still claim their id so lower dirs cannot resurrect them */
		a.name[0] = 0;
	}
	apps = realloc(apps, sizeof *apps * (size_t)(napps + 1));
	apps[napps++] = a;
}

S void apps_load(void)
{
	char dirs[5][256], p[768];
	DIR *d;
	struct dirent *e;
	int i, j;
	for (i = 0; i < napps; i++) icon_free(&apps[i].ic);
	free(apps); apps = NULL; napps = 0;
	snprintf(dirs[0], 256, "%s/.local/share/applications", home());
	snprintf(dirs[1], 256, "%s/.local/share/flatpak/exports/share/applications", home());
	snprintf(dirs[2], 256, "/var/lib/flatpak/exports/share/applications");
	snprintf(dirs[3], 256, "/usr/local/share/applications");
	snprintf(dirs[4], 256, "/usr/share/applications");
	for (i = 0; i < 5; i++) {
		if (!(d = opendir(dirs[i]))) continue;
		while ((e = readdir(d))) {
			size_t l = strlen(e->d_name);
			if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
			snprintf(p, sizeof p, "%s/%s", dirs[i], e->d_name);
			app_parse(p, e->d_name);
		}
		closedir(d);
	}
	for (i = j = 0; i < napps; i++) if (apps[i].name[0]) apps[j++] = apps[i];
	napps = j;
	qsort(apps, (size_t)napps, sizeof *apps, app_cmp);
}

S App *app_find(const char *id)
{
	for (int i = 0; i < napps; i++) if (!strcmp(apps[i].id, id)) return &apps[i];
	return NULL;
}

/* pins may name roles that resolve to whatever is installed */
S App *app_resolve(const char *id)
{
	static const char *files[] = {"thunar.desktop", "pcmanfm.desktop", "nemo.desktop", "org.gnome.Nautilus.desktop", "org.kde.dolphin.desktop", "caja.desktop", NULL};
	static const char *web[] = {"firefox.desktop", "chromium.desktop", "google-chrome.desktop", "brave-browser.desktop", "librewolf.desktop", "org.mozilla.firefox.desktop", NULL};
	static const char *term[] = {"Alacritty.desktop", "alacritty.desktop", "kitty.desktop", "xfce4-terminal.desktop", "xterm.desktop", NULL};
	const char **l = !strcmp(id, "files.desktop") ? files : !strcmp(id, "browser.desktop") ? web :
		!strcmp(id, "terminal.desktop") ? term : NULL;
	App *a;
	if (!l) return app_find(id);
	for (; *l; l++) if ((a = app_find(*l))) return a;
	return NULL;
}

S const char *basename_of(const char *s)
{
	const char *b = strrchr(s, '/');
	return b ? b + 1 : s;
}

S App *app_for_class(const char *cls, const char *inst)
{
	char key[160], *p;
	int i, k;
	if (!cls[0] && !inst[0]) return NULL;
	for (k = 0; k < 2; k++) {
		const char *c = k ? inst : cls;
		if (!c[0]) continue;
		for (i = 0; i < napps; i++) if (apps[i].wmclass[0] && !strcasecmp(apps[i].wmclass, c)) return &apps[i];
		for (i = 0; i < napps; i++) {
			snprintf(key, sizeof key, "%s", apps[i].id);
			if ((p = strstr(key, ".desktop"))) *p = 0;
			if (!strcasecmp(key, c)) return &apps[i];
			if ((p = strrchr(key, '.')) && !strcasecmp(p + 1, c)) return &apps[i];
			snprintf(key, sizeof key, "%s", apps[i].exec);
			key[strcspn(key, " ")] = 0;
			if (!strcasecmp(basename_of(key), c)) return &apps[i];
		}
	}
	return NULL;
}

S void spawn(const char *cmd)
{
	if (fork() == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
}

S void app_launch(App *a)
{
	char cmd[1200], *d = cmd, *end = cmd + 600;
	const char *s;
	if (a->term) d += snprintf(cmd, 600, "%s -e ", cfg.terminal);
	for (s = a->exec; *s && d < end; s++) {
		if (*s == '%') {
			if (s[1] == '%') *d++ = '%';
			if (s[1]) s++;
			continue;
		}
		*d++ = *s;
	}
	*d = 0;
	spawn(cmd);
}

S int run_read(const char *cmd, char *out, int n)
{
	FILE *f = popen(cmd, "r");
	int len = 0;
	out[0] = 0;
	if (!f) return 0;
	len = (int)fread(out, 1, (size_t)n - 1, f);
	out[MAX(len, 0)] = 0;
	pclose(f);
	return len;
}

S int read_int(const char *path)
{
	char b[32] = "";
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	if (!fgets(b, sizeof b, f)) b[0] = 0;
	fclose(f);
	return b[0] ? atoi(b) : -1;
}

/* first battery capacity, -1 when none; *charging set when on AC */
S int battery(int *charging)
{
	char p[300], st[32] = "";
	DIR *d = opendir("/sys/class/power_supply");
	struct dirent *e;
	int cap = -1;
	FILE *f;
	if (charging) *charging = 0;
	if (!d) return -1;
	while ((e = readdir(d)))
		if (!strncmp(e->d_name, "BAT", 3)) {
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
			cap = read_int(p);
			snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name);
			if ((f = fopen(p, "r"))) { if (!fgets(st, sizeof st, f)) st[0] = 0; fclose(f); }
			if (charging) *charging = !strncmp(st, "Charging", 8) || !strncmp(st, "Full", 4);
			break;
		}
	closedir(d);
	return cap;
}

S int volume_get(int *muted)
{
	char b[256];
	float v;
	*muted = 0;
	if (run_read("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", b, sizeof b) && sscanf(b, "Volume: %f", &v) == 1) {
		*muted = strstr(b, "MUTED") != NULL;
		return (int)(v * 100 + .5f);
	}
	if (run_read("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null", b, sizeof b)) {
		char *p = strchr(b, '%');
		while (p && p > b && isdigit((unsigned char)p[-1])) p--;
		if (p) return atoi(p);
	}
	return -1;
}

S void volume_set(int pct)
{
	char cmd[200];
	pct = CLAMP(pct, 0, 100);
	if (have("wpctl")) snprintf(cmd, sizeof cmd, "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0; wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%%", pct);
	else snprintf(cmd, sizeof cmd, "pactl set-sink-mute @DEFAULT_SINK@ 0; pactl set-sink-volume @DEFAULT_SINK@ %d%%", pct);
	spawn(cmd);
}

S int backlight(int *maxv, char *dev, size_t n)
{
	char p[300];
	DIR *d = opendir("/sys/class/backlight");
	struct dirent *e;
	int v = -1;
	if (!d) return -1;
	while ((e = readdir(d)))
		if (e->d_name[0] != '.') {
			snprintf(p, sizeof p, "/sys/class/backlight/%s/max_brightness", e->d_name);
			*maxv = read_int(p);
			snprintf(p, sizeof p, "/sys/class/backlight/%s/brightness", e->d_name);
			v = read_int(p);
			snprintf(dev, n, "%s", e->d_name);
			break;
		}
	closedir(d);
	return *maxv > 0 ? v : -1;
}

S void user_name(char *out, size_t n)
{
	struct passwd *pw = getpwuid(getuid());
	if (pw && pw->pw_gecos && pw->pw_gecos[0] && pw->pw_gecos[0] != ',') {
		snprintf(out, n, "%s", pw->pw_gecos);
		out[strcspn(out, ",")] = 0;
	} else snprintf(out, n, "%s", pw ? pw->pw_name : "User");
}

S void shape_round(Window w, int ww, int hh, int r)
{
	Pixmap m = XCreatePixmap(dpy, w, ww, hh, 1);
	GC gc = XCreateGC(dpy, m, 0, NULL);
	XSetForeground(dpy, gc, 0);
	XFillRectangle(dpy, m, gc, 0, 0, ww, hh);
	XSetForeground(dpy, gc, 1);
	if (r <= 0) XFillRectangle(dpy, m, gc, 0, 0, ww, hh);
	else {
		XFillRectangle(dpy, m, gc, r, 0, ww - 2 * r, hh);
		XFillRectangle(dpy, m, gc, 0, r, ww, hh - 2 * r);
		XFillArc(dpy, m, gc, 0, 0, 2 * r, 2 * r, 0, 360 * 64);
		XFillArc(dpy, m, gc, ww - 2 * r - 1, 0, 2 * r, 2 * r, 0, 360 * 64);
		XFillArc(dpy, m, gc, 0, hh - 2 * r - 1, 2 * r, 2 * r, 0, 360 * 64);
		XFillArc(dpy, m, gc, ww - 2 * r - 1, hh - 2 * r - 1, 2 * r, 2 * r, 0, 360 * 64);
	}
	XShapeCombineMask(dpy, w, ShapeBounding, 0, 0, m, ShapeSet);
	XFreeGC(dpy, gc);
	XFreePixmap(dpy, m);
}

/* tells GTK apps the light/dark choice too */
S void theme_export(void)
{
	char p[512], cmd[400];
	FILE *f;
	if (have("gsettings")) {
		snprintf(cmd, sizeof cmd, "gsettings set org.gnome.desktop.interface color-scheme %s; "
			"gsettings set org.gnome.desktop.interface gtk-theme %s",
			cfg.dark ? "prefer-dark" : "default", cfg.dark ? "Adwaita-dark" : "Adwaita");
		spawn(cmd);
	}
	snprintf(p, sizeof p, "%s/.config/gtk-3.0", home());
	mkdir(p, 0755);
	strcat(p, "/settings.ini");
	if ((f = fopen(p, "r"))) {
		char all[4096] = "", line[512];
		int found = 0;
		while (fgets(line, sizeof line, f)) {
			if (!strncmp(line, "gtk-application-prefer-dark-theme", 33)) {
				snprintf(line, sizeof line, "gtk-application-prefer-dark-theme=%d\n", cfg.dark);
				found = 1;
			}
			if (strlen(all) + strlen(line) < sizeof all) strcat(all, line);
		}
		fclose(f);
		if (!found && strstr(all, "[Settings]")) {
			char *s = strstr(all, "[Settings]") + 10, rest[4096];
			snprintf(rest, sizeof rest, "%s", s);
			snprintf(s, sizeof all - (size_t)(s - all), "\ngtk-application-prefer-dark-theme=%d%s", cfg.dark, rest);
		} else if (!found) {
			size_t l = strlen(all);
			snprintf(all + l, sizeof all - l, "[Settings]\ngtk-application-prefer-dark-theme=%d\n", cfg.dark);
		}
		if ((f = fopen(p, "w"))) { fputs(all, f); fclose(f); }
	} else if ((f = fopen(p, "w"))) {
		fprintf(f, "[Settings]\ngtk-application-prefer-dark-theme=%d\n", cfg.dark);
		fclose(f);
	}
}

#endif
