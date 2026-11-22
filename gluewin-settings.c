/* gluewin-settings: Windows 11 style settings, plain Xlib/Xft */
#include "common.h"
#include <sys/utsname.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <webp/decode.h>

enum { NAVW = 290, PAD = 28, RH = 68, CWMAX = 920, HEAD = 86 };
enum { P_SYS, P_BT, P_NET, P_PERS, P_APPS, P_LANG, P_UPD, NPAGE };
enum { A_NAV = 1, A_TOGGLE, A_DROP, A_SLIDER, A_BTN, A_WIFI, A_BTDEV, A_SWATCH, A_WALL, A_UNINST, A_KBDEL, A_KBUP };
enum { T_BT, T_WIFI, T_TILING };
enum { D_MODE, D_ALIGN, D_BAR, D_KBADD, D_LANG, D_RES = 10 };
enum { S_BRIGHT, S_VOL };
enum { B_DISPADV, B_SOUNDADV, B_BTADD, B_NETADV, B_BROWSE, B_UPDATE, B_PWOK, B_PWCANCEL, B_RESCAN };

static const int pg_glyph[NPAGE] = {G_SYSTEM, G_BT, G_WIFI, G_BRUSH, G_APPS, G_KEYBOARD, G_UPDATE};
static const char *pg_id[NPAGE] = {"system", "bluetooth", "network", "personalization", "apps", "language", "update"};
static const unsigned accents[] = {0x0067C0, 0x0078D4, 0x0099BC, 0x2D7D9A, 0x00B294, 0x10893E,
	0x498205, 0xCA5010, 0xE81123, 0xC30052, 0x8764B8, 0x744DA9, 0x5D5A58, 0x767676};

static const char *pg_name(int p)
{
	switch (p) {
	case P_SYS: return T("System", "Sistem");
	case P_BT: return T("Bluetooth & devices", "Bluetooth și dispozitive");
	case P_NET: return T("Network & internet", "Rețea și internet");
	case P_PERS: return T("Personalization", "Personalizare");
	case P_APPS: return T("Apps", "Aplicații");
	case P_LANG: return T("Time & language", "Oră și limbă");
	default: return T("Update", "Actualizări");
	}
}

static Window win;
static Atom WM_DEL;
static int nav = NAVW, ww = 1040, wh = 700, page, scroll, content_h, mx = -1, my = -1;
static char status_msg[160], self[512];
static long refresh_at;

typedef struct { int x, y, w, h, act, arg; } Hot;
static Hot hot[400];
static int nhot, hover = -1;

typedef struct { char name[32], modes[24][24]; int nm, cur; } Out;
static Out outs[4];
static int nouts, bl_max, bl_val, vol = -1, muted, batt = -1, charging;
static char bl_dev[64], about[5][160];

typedef struct { char mac[18], name[64]; int conn; } BtDev;
static BtDev bts[16];
static int nbts, bt_avail, bt_on;

typedef struct { char ssid[64]; int sig, sec, inuse; } Net;
static Net nets[40];
static int nnets, nm_avail, wifi_on;
static char eth[96], pw_ssid[64], pw_buf[128];
static int pw_on;

static char walls[10][256], last_upd[64];
static int nwalls;
static Picture thumb[10];
static char thumb_tried[10];
enum { TBW = 160, TBH = 90 };

static int drop = -1, drop_x, drop_y, drop_w, drop_n, drop_sel, drop_hover = -1;
static char drop_opt[24][48];
static int slide = -1, sl_x[2], sl_w[2];

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* fork+exec without a shell, so SSIDs and passwords are never parsed */
static void run(const char *a0, ...)
{
	const char *argv[16];
	int n = 0;
	va_list ap;
	va_start(ap, a0);
	for (argv[n++] = a0; n < 15 && (argv[n] = va_arg(ap, const char *)); n++);
	va_end(ap);
	argv[n] = NULL;
	if (fork() == 0) {
		close(ConnectionNumber(dpy));
		setsid();
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
}

static void later(const char *msg, int ms)
{
	snprintf(status_msg, sizeof status_msg, "%s", msg ? msg : "");
	refresh_at = now_ms() + ms;
}

/* ---------- data ---------- */

static void load_sys(void)
{
	char b[16384], *l, *sv;
	Out *o = NULL;
	nouts = 0;
	if (run_read("xrandr --query 2>/dev/null", b, sizeof b))
		for (l = strtok_r(b, "\n", &sv); l; l = strtok_r(NULL, "\n", &sv)) {
			if (!isspace((unsigned char)l[0])) {
				o = NULL;
				if (strstr(l, " connected") && nouts < 4) {
					o = &outs[nouts++];
					memset(o, 0, sizeof *o);
					o->cur = -1;
					sscanf(l, "%31s", o->name);
				}
			} else if (o && o->nm < 24) {
				char m[24];
				if (sscanf(l, "%23s", m) != 1 || strchr(m, 'i')) continue;
				if (o->nm && !strcmp(o->modes[o->nm - 1], m)) continue;
				if (strchr(l, '*')) o->cur = o->nm;
				strcpy(o->modes[o->nm++], m);
			}
		}
	bl_val = backlight(&bl_max, bl_dev, sizeof bl_dev);
	vol = volume_get(&muted);
	batt = battery(&charging);
	if (!about[0][0]) {
		struct utsname u;
		FILE *f;
		char line[256];
		gethostname(about[0], sizeof about[0]);
		snprintf(about[1], sizeof about[1], "Linux");
		if ((f = fopen("/etc/os-release", "r"))) {
			while (fgets(line, sizeof line, f))
				if (!strncmp(line, "PRETTY_NAME=", 12)) {
					char *s = line + 12 + (line[12] == '"');
					s[strcspn(s, "\"\n")] = 0;
					snprintf(about[1], sizeof about[1], "%s", s);
				}
			fclose(f);
		}
		if ((f = fopen("/proc/cpuinfo", "r"))) {
			while (fgets(line, sizeof line, f))
				if (!strncmp(line, "model name", 10) && strchr(line, ':')) {
					char *s = strchr(line, ':') + 2;
					s[strcspn(s, "\n")] = 0;
					snprintf(about[2], sizeof about[2], "%s", s);
					break;
				}
			fclose(f);
		}
		if ((f = fopen("/proc/meminfo", "r"))) {
			long kb = 0;
			if (fgets(line, sizeof line, f)) sscanf(line, "MemTotal: %ld", &kb);
			snprintf(about[3], sizeof about[3], "%.1f GB", kb / 1048576.0);
			fclose(f);
		}
		if (!uname(&u)) snprintf(about[4], sizeof about[4], "%s", u.release);
	}
}

static void load_bt(void)
{
	char b[4096], c[4096], *l, *sv;
	nbts = 0;
	bt_avail = have("bluetoothctl") && run_read("timeout 2 bluetoothctl show 2>/dev/null", b, sizeof b) && strstr(b, "Controller");
	if (!bt_avail) return;
	bt_on = strstr(b, "Powered: yes") != NULL;
	if (!run_read("timeout 2 bluetoothctl devices Paired 2>/dev/null", b, sizeof b) || !strstr(b, "Device "))
		run_read("timeout 2 bluetoothctl paired-devices 2>/dev/null", b, sizeof b);
	run_read("timeout 2 bluetoothctl devices Connected 2>/dev/null", c, sizeof c);
	for (l = strtok_r(b, "\n", &sv); l && nbts < 16; l = strtok_r(NULL, "\n", &sv)) {
		BtDev *d = &bts[nbts];
		if (sscanf(l, "Device %17s %63[^\n]", d->mac, d->name) != 2) continue;
		d->conn = strstr(c, d->mac) != NULL;
		nbts++;
	}
}

/* splits one nmcli -t line, honouring \: escapes */
static int nm_split(char *l, char **f, int max)
{
	int n = 0;
	char *w = l;
	f[n++] = w;
	for (char *r = l; *r; r++) {
		if (*r == '\\' && r[1]) { *w++ = *++r; continue; }
		if (*r == ':' && n < max) { *w++ = 0; f[n++] = w; continue; }
		*w++ = *r;
	}
	*w = 0;
	return n;
}

static void load_net(void)
{
	char b[16384], *l, *sv, *f[5];
	nnets = 0;
	eth[0] = 0;
	nm_avail = have("nmcli");
	if (!nm_avail) return;
	run_read("nmcli -t -f WIFI g 2>/dev/null", b, sizeof b);
	wifi_on = !strncmp(b, "enabled", 7);
	if (run_read("nmcli -t -f TYPE,STATE,CONNECTION dev 2>/dev/null", b, sizeof b))
		for (l = strtok_r(b, "\n", &sv); l; l = strtok_r(NULL, "\n", &sv))
			if (nm_split(l, f, 3) == 3 && !strcmp(f[0], "ethernet") && !eth[0])
				snprintf(eth, sizeof eth, "%s", !strncmp(f[1], "connected", 9) ? f[2] : "");
	if (!wifi_on || !run_read("nmcli -t -f IN-USE,SIGNAL,SECURITY,SSID dev wifi list --rescan no 2>/dev/null", b, sizeof b)) return;
	for (l = strtok_r(b, "\n", &sv); l; l = strtok_r(NULL, "\n", &sv)) {
		int i;
		if (nm_split(l, f, 4) != 4 || !f[3][0]) continue;
		for (i = 0; i < nnets && strcmp(nets[i].ssid, f[3]); i++);
		if (i == nnets) { if (nnets == 40) continue; nnets++; memset(&nets[i], 0, sizeof nets[i]); }
		snprintf(nets[i].ssid, sizeof nets[i].ssid, "%s", f[3]);
		nets[i].sig = MAX(nets[i].sig, atoi(f[1]));
		nets[i].sec = f[2][0] && strcmp(f[2], "--");
		nets[i].inuse |= f[0][0] == '*';
	}
	for (int i = 0; i < nnets; i++)
		if (nets[i].inuse && i) { Net t = nets[0]; nets[0] = nets[i]; nets[i] = t; }
}

static void scan_walls(const char *dir, int depth)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char p[512];
	if (!d) return;
	while ((e = readdir(d)) && nwalls < (int)LEN(walls)) {
		const char *x = strrchr(e->d_name, '.');
		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		if (x && (!strcasecmp(x, ".png") || !strcasecmp(x, ".jpg") || !strcasecmp(x, ".jpeg") || !strcasecmp(x, ".webp")))
			snprintf(walls[nwalls++], sizeof walls[0], "%s", p);
		else if (depth > 0 && e->d_type == DT_DIR) scan_walls(p, depth - 1);
	}
	closedir(d);
}

struct jerr { struct jpeg_error_mgr m; jmp_buf jb; };
static void jpeg_fail(j_common_ptr c) { longjmp(((struct jerr *)c->err)->jb, 1); }

/* decodes to 0xAARRGGBB; jpeg is decoded at 1/2..1/8 scale to stay cheap */
static unsigned *decode(const char *path, int *w, int *h)
{
	const char *x = strrchr(path, '.');
	unsigned *px = NULL;
	if (!x) return NULL;
	if (!strcasecmp(x, ".png")) {
		png_image im;
		memset(&im, 0, sizeof im);
		im.version = PNG_IMAGE_VERSION;
		if (!png_image_begin_read_from_file(&im, path)) return NULL;
		im.format = PNG_FORMAT_BGRA;
		if (im.width > 8192 || im.height > 8192 || !(px = malloc(PNG_IMAGE_SIZE(im)))) { png_image_free(&im); return NULL; }
		if (!png_image_finish_read(&im, NULL, px, 0, NULL)) { free(px); return NULL; }
		*w = (int)im.width; *h = (int)im.height;
	} else if (!strcasecmp(x, ".webp")) {
		FILE *f = fopen(path, "rb");
		long n;
		unsigned char *d;
		if (!f) return NULL;
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		rewind(f);
		if (n > 0 && n < (64 << 20) && (d = malloc((size_t)n))) {
			if (fread(d, 1, (size_t)n, f) == (size_t)n) px = (unsigned *)WebPDecodeBGRA(d, (size_t)n, w, h);
			free(d);
		}
		fclose(f);
	} else {
		struct jpeg_decompress_struct c;
		struct jerr e;
		FILE *f = fopen(path, "rb");
		if (!f) return NULL;
		c.err = jpeg_std_error(&e.m);
		e.m.error_exit = jpeg_fail;
		if (setjmp(e.jb)) { jpeg_destroy_decompress(&c); fclose(f); free(px); return NULL; }
		jpeg_create_decompress(&c);
		jpeg_stdio_src(&c, f);
		jpeg_read_header(&c, TRUE);
		c.out_color_space = JCS_EXT_BGRX;
		c.scale_num = 1;
		for (c.scale_denom = 8; c.scale_denom > 1 && (c.image_width / c.scale_denom < TBW * 2 || c.image_height / c.scale_denom < TBH * 2); c.scale_denom /= 2);
		jpeg_start_decompress(&c);
		*w = (int)c.output_width; *h = (int)c.output_height;
		px = malloc((size_t)*w * (size_t)*h * 4);
		while (c.output_scanline < c.output_height) {
			JSAMPROW r = (JSAMPROW)(px + (size_t)c.output_scanline * (size_t)*w);
			jpeg_read_scanlines(&c, &r, 1);
		}
		jpeg_finish_decompress(&c);
		jpeg_destroy_decompress(&c);
		fclose(f);
	}
	return px;
}

/* box-filtered, center-cropped to 16:9 like Windows' picture tiles */
static Picture make_thumb(const char *path)
{
	int w, h, cx, cy, cw, ch;
	unsigned *src = decode(path, &w, &h), *dst;
	if (!src) return 0;
	cw = w; ch = w * TBH / TBW;
	if (ch > h) { ch = h; cw = h * TBW / TBH; }
	cx = (w - cw) / 2; cy = (h - ch) / 2;
	dst = malloc(TBW * TBH * 4);
	for (int y = 0; y < TBH; y++)
		for (int x = 0; x < TBW; x++) {
			int x0 = cx + x * cw / TBW, x1 = MAX(x0 + 1, cx + (x + 1) * cw / TBW);
			int y0 = cy + y * ch / TBH, y1 = MAX(y0 + 1, cy + (y + 1) * ch / TBH);
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy++)
				for (int xx = x0; xx < x1; xx++) {
					unsigned p = src[(size_t)yy * (size_t)w + (size_t)xx];
					r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
				}
			dst[y * TBW + x] = 0xFF000000u | (r / n) << 16 | (g / n) << 8 | (b / n);
		}
	if (strcasecmp(strrchr(path, '.'), ".webp")) free(src);
	else WebPFree(src);
	Pixmap pm = XCreatePixmap(dpy, root, TBW, TBH, depth);
	XImage *im = XCreateImage(dpy, vis, depth, ZPixmap, 0, (char *)dst, TBW, TBH, 32, 0);
	XPutImage(dpy, pm, DefaultGC(dpy, scr), im, 0, 0, 0, 0, TBW, TBH);
	XDestroyImage(im);
	Picture p = XRenderCreatePicture(dpy, pm, fmt, 0, NULL);
	XFreePixmap(dpy, pm);
	return p;
}

static void load_pers(void)
{
	char p[512];
	for (int i = 0; i < (int)LEN(thumb); i++) { if (thumb[i]) XRenderFreePicture(dpy, thumb[i]); thumb[i] = 0; thumb_tried[i] = 0; }
	nwalls = 0;
	scan_walls("/usr/share/backgrounds", 2);
	snprintf(p, sizeof p, "%s/Pictures/Wallpapers", home());
	scan_walls(p, 1);
	snprintf(p, sizeof p, "%s/Pictures", home());
	scan_walls(p, 0);
}

static void load_upd(void)
{
	char line[512];
	FILE *f = fopen("/var/log/pacman.log", "r");
	last_upd[0] = 0;
	if (!f) return;
	while (fgets(line, sizeof line, f))
		if (strstr(line, "starting full system upgrade") && line[0] == '[')
			snprintf(last_upd, sizeof last_upd, "%.10s %.5s", line + 1, line + 12);
	fclose(f);
}

static void load_lang(void);

static void load_page(void)
{
	switch (page) {
	case P_SYS: load_sys(); break;
	case P_BT: load_bt(); break;
	case P_NET: load_net(); break;
	case P_PERS: load_pers(); break;
	case P_APPS: if (!napps) apps_load(); break;
	case P_UPD: load_upd(); break;
	case P_LANG: load_lang(); break;
	}
}

/* ---------- widgets ---------- */

static int hov(int x, int y, int w, int h) { return mx >= x && mx < x + w && my >= y && my < y + h; }

static void addhot(int x, int y, int w, int h, int act, int arg)
{
	if (nhot < (int)LEN(hot)) hot[nhot++] = (Hot){x, y, w, h, act, arg};
}

static void card(int x, int y, int w, int h) { rbox(x, y, w, h, 6, th.card, th.border); }

static void row(int x, int y, int w, int g, const char *title, const char *sub)
{
	card(x, y, w, RH);
	if (g >= 0) glyph(g, x + 20, y + (RH - 22) / 2, 22, th.text);
	int tx = x + (g >= 0 ? 62 : 20);
	if (sub && *sub) {
		textfit(F_ui, tx, y + 14, w / 2 - 50, title, th.text);
		textfit(F_sm, tx, y + 36, w / 2 - 50, sub, th.sub);
	} else textfit(F_ui, tx, y + (RH - fh(F_ui)) / 2, w / 2 - 50, title, th.text);
}

static void section(int x, int *y, const char *s)
{
	text(F_md, x + 2, *y + 6, s, th.text);
	*y += 34;
}

static void toggle(int rx, int y, int on, int id)
{
	int x = rx - 40 - 20, ty = y + (RH - 20) / 2;
	const char *lbl = on ? T("On", "Activat") : T("Off", "Dezactivat");
	text(F_ui, x - 12 - tw(F_ui, lbl), ty + (20 - fh(F_ui)) / 2, lbl, th.text);
	if (on) {
		rrect(x, ty, 40, 20, 10, th.accent, 255);
		rrect(x + 24, ty + 4, 12, 12, 6, th.onaccent, 255);
	} else {
		rrect(x, ty, 40, 20, 10, th.sub, 255);
		rrect(x + 1, ty + 1, 38, 18, 9, th.card, 255);
		rrect(x + 5, ty + 5, 10, 10, 5, th.sub, 255);
	}
	addhot(x - 12 - tw(F_ui, lbl), ty - 4, 52 + tw(F_ui, lbl) + 4, 28, A_TOGGLE, id);
}

static void dropbtn(int rx, int y, int w, const char *lbl, int id)
{
	int x = rx - w - 20, by = y + (RH - 32) / 2;
	rbox(x, by, w, 32, 4, hov(x, by, w, 32) ? th.hover : th.menu, th.border);
	textfit(F_ui, x + 12, by + (32 - fh(F_ui)) / 2, w - 40, lbl, th.text);
	glyph(G_CHEVD, x + w - 28, by + 8, 16, th.text);
	addhot(x, by, w, 32, A_DROP, id);
}

static int button(int x, int y, const char *lbl, int primary, int act, int arg)
{
	int w = tw(F_ui, lbl) + 32, h = 32;
	unsigned bg = primary ? th.accent : (hov(x, y, w, h) ? th.hover : th.menu);
	if (primary) rrect(x, y, w, h, 4, hov(x, y, w, h) ? mix(th.accent, th.card, 40) : th.accent, 255);
	else rbox(x, y, w, h, 4, bg, th.border);
	textc(F_ui, x, y + (h - fh(F_ui)) / 2, w, lbl, primary ? th.onaccent : th.text);
	addhot(x, y, w, h, act, arg);
	return w;
}

static void rbutton(int rx, int y, const char *lbl, int primary, int act, int arg)
{
	int w = tw(F_ui, lbl) + 32;
	button(rx - w - 20, y + (RH - 32) / 2, lbl, primary, act, arg);
}

static void slider(int x, int y, int w, int pct, int id)
{
	int cy = y + (RH - 4) / 2, k = x + w * CLAMP(pct, 0, 100) / 100;
	rrect(x, cy, w, 4, 2, th.sub, 255);
	rrect(x, cy, k - x, 4, 2, th.accent, 255);
	rrect(k - 10, cy - 8, 20, 20, 10, th.card, 255);
	rrect(k - 10, cy - 8, 20, 20, 10, th.border, 255);
	rrect(k - 9, cy - 7, 18, 18, 9, th.card, 255);
	rrect(k - 5, cy - 3, 10, 10, 5, th.accent, 255);
	sl_x[id] = x;
	sl_w[id] = w;
	addhot(x - 10, y + 10, w + 20, RH - 20, A_SLIDER, id);
}

static void valtext(int rx, int y, const char *s)
{
	text(F_ui, rx - 20 - tw(F_ui, s), y + (RH - fh(F_ui)) / 2, s, th.sub);
}

/* ---------- pages ---------- */

static int pg_sys(int x, int y, int w)
{
	char s[160];
	section(x, &y, T("Display", "Ecran"));
	if (bl_max > 0) {
		row(x, y, w, G_SUN, T("Brightness", "Luminozitate"), NULL);
		slider(x + w - 30 - MIN(260, w / 2 - 40), y, MIN(260, w / 2 - 40), bl_val * 100 / bl_max, S_BRIGHT);
		y += RH + 4;
	}
	for (int i = 0; i < nouts; i++) {
		snprintf(s, sizeof s, "%s (%s)", T("Display resolution", "Rezoluția ecranului"), outs[i].name);
		row(x, y, w, G_SYSTEM, s, NULL);
		dropbtn(x + w, y, 200, outs[i].cur >= 0 ? outs[i].modes[outs[i].cur] : "-", D_RES + i);
		y += RH + 4;
	}
	if (have("arandr")) {
		row(x, y, w, G_SYSTEM, T("Advanced display", "Afișare avansată"), T("Arrange multiple displays", "Aranjare monitoare multiple"));
		rbutton(x + w, y, T("Open", "Deschide"), 0, A_BTN, B_DISPADV);
		y += RH + 4;
	}
	y += 16;
	section(x, &y, T("Sound", "Sunet"));
	row(x, y, w, muted ? G_MUTE : G_VOL, T("Volume", "Volum"), vol < 0 ? T("No audio device", "Niciun dispozitiv audio") : NULL);
	if (vol >= 0) slider(x + w - 30 - MIN(260, w / 2 - 40), y, MIN(260, w / 2 - 40), vol, S_VOL);
	y += RH + 4;
	if (have("pavucontrol")) {
		row(x, y, w, G_VOL, T("More sound settings", "Mai multe setări de sunet"), T("Devices, inputs, per-app volume", "Dispozitive, microfon, volum per aplicație"));
		rbutton(x + w, y, T("Open", "Deschide"), 0, A_BTN, B_SOUNDADV);
		y += RH + 4;
	}
	if (batt >= 0) {
		y += 16;
		section(x, &y, T("Power & battery", "Alimentare și baterie"));
		row(x, y, w, G_BATT, T("Battery", "Baterie"), charging ? T("Plugged in", "Conectat la priză") : T("On battery", "Pe baterie"));
		snprintf(s, sizeof s, "%d%%", batt);
		valtext(x + w, y, s);
		y += RH + 4;
	}
	y += 16;
	section(x, &y, T("About", "Despre"));
	const char *k[5] = {T("Device name", "Nume dispozitiv"), T("Operating system", "Sistem de operare"),
		T("Processor", "Procesor"), T("Installed RAM", "Memorie RAM instalată"), "Kernel"};
	card(x, y, w, 5 * 34 + 20);
	for (int i = 0; i < 5; i++) {
		text(F_ui, x + 20, y + 12 + i * 34 + 6, k[i], th.sub);
		textfit(F_ui, x + 240, y + 12 + i * 34 + 6, w - 260, about[i], th.text);
	}
	return y + 5 * 34 + 20;
}

static int pg_bt(int x, int y, int w)
{
	char s[96];
	if (!bt_avail) {
		row(x, y, w, G_BT, "Bluetooth", T("No Bluetooth adapter or service found", "Nu s-a găsit adaptor sau serviciu Bluetooth"));
		return y + RH;
	}
	row(x, y, w, G_BT, "Bluetooth", T("Discoverable as this computer", "Vizibil ca acest computer"));
	toggle(x + w, y, bt_on, T_BT);
	y += RH + 4;
	row(x, y, w, G_PLUS, T("Devices", "Dispozitive"), T("Pair a mouse, keyboard, headphones", "Asociază mouse, tastatură, căști"));
	rbutton(x + w, y, T("Add device", "Adaugă dispozitiv"), 1, A_BTN, B_BTADD);
	y += RH + 20;
	if (nbts) section(x, &y, T("Paired devices", "Dispozitive asociate"));
	for (int i = 0; i < nbts; i++) {
		snprintf(s, sizeof s, "%s", bts[i].conn ? T("Connected", "Conectat") : T("Paired", "Asociat"));
		row(x, y, w, G_BT, bts[i].name, s);
		if (bt_on) rbutton(x + w, y, bts[i].conn ? T("Disconnect", "Deconectează") : T("Connect", "Conectează"), 0, A_BTDEV, i);
		y += RH + 4;
	}
	return y;
}

static int pg_net(int x, int y, int w)
{
	char s[96];
	if (!nm_avail) {
		row(x, y, w, G_WIFIOFF, "Wi-Fi", T("NetworkManager is not installed", "NetworkManager nu este instalat"));
		return y + RH;
	}
	if (eth[0] || !wifi_on) {
		row(x, y, w, G_ETH, "Ethernet", eth[0] ? eth : T("Not connected", "Neconectat"));
		y += RH + 4;
	}
	row(x, y, w, wifi_on ? G_WIFI : G_WIFIOFF, "Wi-Fi", nnets && nets[0].inuse ? nets[0].ssid : NULL);
	toggle(x + w, y, wifi_on, T_WIFI);
	y += RH + 4;
	if (have("nm-connection-editor")) {
		row(x, y, w, G_GEAR, T("Advanced network settings", "Setări avansate de rețea"), T("VPN, proxy, static IP", "VPN, proxy, IP static"));
		rbutton(x + w, y, T("Open", "Deschide"), 0, A_BTN, B_NETADV);
		y += RH + 4;
	}
	if (!wifi_on) return y;
	y += 16;
	textfit(F_md, x + 2, y + 6, w - tw(F_ui, T("Refresh", "Reîmprospătează")) - 50, T("Available networks", "Rețele disponibile"), th.text);
	button(x + w - tw(F_ui, T("Refresh", "Reîmprospătează")) - 32, y - 2, T("Refresh", "Reîmprospătează"), 0, A_BTN, B_RESCAN);
	y += 40;
	for (int i = 0; i < nnets; i++) {
		snprintf(s, sizeof s, "%s%s%d%%", nets[i].inuse ? T("Connected, ", "Conectat, ") : "",
			nets[i].sec ? T("secured, ", "securizată, ") : T("open, ", "deschisă, "), nets[i].sig);
		row(x, y, w, nets[i].sig > 30 ? G_WIFI : G_WIFIOFF, nets[i].ssid, s);
		if (nets[i].sec) glyph(G_LOCK, x + 36, y + 38, 12, th.text);
		if (pw_on && !strcmp(pw_ssid, nets[i].ssid)) {
			int bx = x + w - 20, fw = 260;
			bx -= button(bx - tw(F_ui, T("Cancel", "Anulează")) - 32, y + 18, T("Cancel", "Anulează"), 0, A_BTN, B_PWCANCEL) + 8;
			bx -= button(bx - tw(F_ui, T("Next", "Înainte")) - 32, y + 18, T("Next", "Înainte"), 1, A_BTN, B_PWOK) + 8;
			char dots[130];
			int n = MIN((int)strlen(pw_buf), 40);
			memset(dots, '*', (size_t)n);
			dots[n] = 0;
			rbox(bx - fw, y + 18, fw, 32, 4, th.menu, th.accent);
			rrect(bx - fw + 1, y + 48, fw - 2, 2, 1, th.accent, 255);
			if (n) text(F_ui, bx - fw + 10, y + 18 + (32 - fh(F_ui)) / 2, dots, th.text);
			else text(F_ui, bx - fw + 10, y + 18 + (32 - fh(F_ui)) / 2, T("Network security key", "Cheia de securitate"), th.sub);
			fill(bx - fw + 10 + (n ? tw(F_ui, dots) : 0) + 1, y + 25, 1, 18, th.text);
		} else
			rbutton(x + w, y, nets[i].inuse ? T("Disconnect", "Deconectează") : T("Connect", "Conectează"), !nets[i].inuse, A_WIFI, i);
		y += RH + 4;
	}
	if (!nnets) { row(x, y, w, G_WIFIOFF, T("No networks found", "Nicio rețea găsită"), NULL); y += RH; }
	return y;
}

static int pg_pers(int x, int y, int w)
{
	section(x, &y, T("Colors", "Culori"));
	row(x, y, w, G_MOON, T("Choose your mode", "Alege modul"), T("Light or dark windows and taskbar", "Ferestre și bară luminoase sau întunecate"));
	dropbtn(x + w, y, 160, cfg.dark ? T("Dark", "Întunecat") : T("Light", "Luminos"), D_MODE);
	y += RH + 4;
	int n = (int)LEN(accents), sw = 36, per = MAX(1, (w - 82) / (sw + 8)), gh = 40 + (n + per - 1) / per * (sw + 8) + 12;
	card(x, y, w, gh);
	glyph(G_BRUSH, x + 20, y + 16, 22, th.text);
	text(F_ui, x + 62, y + 18, T("Accent color", "Culoare de accent"), th.text);
	for (int i = 0, cx = x + 62, cy = y + 50; i < n; i++, cx += sw + 8) {
		if (cx + sw > x + w - 20) { cx = x + 62; cy += sw + 8; }
		if (accents[i] == cfg.accent) { rrect(cx - 3, cy - 3, sw + 6, sw + 6, 7, th.text, 255); rrect(cx - 1, cy - 1, sw + 2, sw + 2, 5, th.card, 255); }
		rrect(cx, cy, sw, sw, 4, accents[i], 255);
		if (accents[i] == cfg.accent) glyph(G_CHECK, cx + 8, cy + 8, 20, 0xFFFFFF);
		addhot(cx, cy, sw, sw, A_SWATCH, i);
	}
	y += gh + 20;
	section(x, &y, T("Taskbar", "Bara de activități"));
	row(x, y, w, G_START, T("Taskbar alignment", "Alinierea barei"), NULL);
	dropbtn(x + w, y, 160, cfg.align_left ? T("Left", "Stânga") : T("Center", "Centru"), D_ALIGN);
	y += RH + 4;
	row(x, y, w, G_SYSTEM, T("Taskbar position", "Poziția barei"), NULL);
	dropbtn(x + w, y, 160, cfg.bar_top ? T("Top", "Sus") : T("Bottom", "Jos"), D_BAR);
	y += RH + 20;
	section(x, &y, T("Multitasking", "Multitasking"));
	row(x, y, w, G_APPS, T("Tile windows automatically", "Aranjează ferestrele automat (tiling)"),
		T("Win+T toggles, Win+Shift+F floats a window", "Win+T pornește/oprește, Win+Shift+F scoate o fereastră"));
	toggle(x + w, y, cfg.tiling, T_TILING);
	y += RH + 20;
	section(x, &y, T("Background", "Fundal"));
	row(x, y, w, G_FOLDER, T("Choose a photo", "Alege o fotografie"), cfg.wallpaper[0] ? basename_of(cfg.wallpaper) : NULL);
	if (have("zenity") || have("kdialog")) rbutton(x + w, y, T("Browse photos", "Răsfoiește"), 0, A_BTN, B_BROWSE);
	y += RH + 4;
	if (!nwalls) return y;
	int tp = MAX(1, (w - 40 + 12) / (TBW + 12)), rows = (nwalls + tp - 1) / tp;
	card(x, y, w, rows * (TBH + 12) + 28);
	text(F_sm, x + 20, y + 10, T("Recent images", "Imagini recente"), th.sub);
	for (int i = 0; i < nwalls; i++) {
		int tx = x + 20 + (i % tp) * (TBW + 12), ty = y + 32 + (i / tp) * (TBH + 12);
		if (!thumb_tried[i] && ty < wh && ty + TBH > 0) { thumb_tried[i] = 1; thumb[i] = make_thumb(walls[i]); }
		if (!strcmp(walls[i], cfg.wallpaper)) rrect(tx - 3, ty - 3, TBW + 6, TBH + 6, 6, th.accent, 255);
		else if (hov(tx, ty, TBW, TBH)) rrect(tx - 2, ty - 2, TBW + 4, TBH + 4, 5, th.sub, 255);
		if (thumb[i]) XRenderComposite(dpy, PictOpSrc, thumb[i], None, B.pic, 0, 0, 0, 0, tx, ty, TBW, TBH);
		else { rrect(tx, ty, TBW, TBH, 4, th.hover, 255); textfit(F_sm, tx + 8, ty + TBH / 2 - 7, TBW - 16, basename_of(walls[i]), th.sub); }
		addhot(tx, ty, TBW, TBH, A_WALL, i);
	}
	return y + rows * (TBH + 12) + 28;
}

static int pg_apps(int x, int y, int w)
{
	section(x, &y, T("Installed apps", "Aplicații instalate"));
	for (int i = 0; i < napps; i++) {
		App *a = &apps[i];
		if (y > -RH && y < wh) {
			card(x, y, w, 56);
			if (app_icon(a)) icon_draw(&a->ic, x + 16, y + 12, 32);
			else letter_tile(a->name, x + 16, y + 12, 32);
			const char *l = T("Uninstall", "Dezinstalează");
			textfit(F_ui, x + 62, y + 9, w - tw(F_ui, l) - 130, a->name, th.text);
			textfit(F_sm, x + 62, y + 30, w - tw(F_ui, l) - 130, a->id, th.sub);
			button(x + w - tw(F_ui, l) - 52, y + 12, l, 0, A_UNINST, i);
		}
		y += 60;
	}
	return y;
}

static char kb[8][16], kbadd[24][16];
static int nkb, nkbadd, lang_changed;

static void apply_cfg(int dark_changed);

static void load_lang(void)
{
	nkb = cfg.keyboard[0] ? kbd_split(cfg.keyboard, kb, (int)LEN(kb)) : kbd_current(kb, (int)LEN(kb));
	if (!nkb) { strcpy(kb[0], "us"); nkb = 1; }
}

static void kb_save(void)
{
	cfg.keyboard[0] = 0;
	for (int i = 0; i < nkb; i++) {
		size_t l = strlen(cfg.keyboard);
		snprintf(cfg.keyboard + l, sizeof cfg.keyboard - l, "%s%s", i ? "," : "", kb[i]);
	}
	apply_cfg(0);
}

static int pg_lang(int x, int y, int w)
{
	char s[160];
	time_t now = time(NULL);
	section(x, &y, T("Date & time", "Data și ora"));
	strftime(s, sizeof s, "%H:%M   %d.%m.%Y", localtime(&now));
	row(x, y, w, G_INFO, T("Current date and time", "Data și ora curente"), getenv("TZ") ? getenv("TZ") : NULL);
	valtext(x + w, y, s);
	y += RH + 4;
	section(x, &y, T("Language", "Limbă"));
	row(x, y, w, G_GLOBE, T("Display language", "Limba de afișare"),
		lang_changed ? T("Sign out to apply it to all apps", "Deconectează-te ca să se aplice în toate aplicațiile")
		: T("Menus, Settings and apps use this language", "Meniurile, Setările și aplicațiile folosesc această limbă"));
	dropbtn(x + w, y, MIN(200, w / 2 - 20), RO ? "Română" : "English", D_LANG);
	y += RH + 4;
	section(x, &y, T("Keyboard", "Tastatură"));
	for (int i = 0; i < nkb; i++) {
		const Kbd *k = kbd_info(kb[i]);
		snprintf(s, sizeof s, "%s  ·  %s", kb[i], i ? T("Switch with Win+Space", "Comuți cu Win+Space") : T("Default", "Implicită"));
		row(x, y, w, G_KEYBOARD, k ? T(k->en, k->ro) : kb[i], s);
		int bx = x + w - 20;
		if (nkb > 1) { const char *l = T("Remove", "Elimină"); bx -= tw(F_ui, l) + 32; button(bx, y + (RH - 32) / 2, l, 0, A_KBDEL, i); }
		if (i) { const char *l = T("Make default", "Fă implicită"); bx -= tw(F_ui, l) + 32 + 8; button(bx, y + (RH - 32) / 2, l, 0, A_KBUP, i); }
		y += RH + 4;
	}
	nkbadd = 0;
	for (int i = 0; i < (int)LEN(kbds) && nkbadd < (int)LEN(kbadd); i++) {
		int have_it = 0;
		for (int j = 0; j < nkb; j++) have_it |= !strcmp(kb[j], kbds[i].code);
		if (!have_it) strcpy(kbadd[nkbadd++], kbds[i].code);
	}
	if (nkb < (int)LEN(kb) && nkbadd) {
		row(x, y, w, G_PLUS, T("Add a keyboard", "Adaugă o tastatură"), T("Windows key + Space switches between keyboards", "Tasta Windows + Space comută între tastaturi"));
		dropbtn(x + w, y, MIN(240, w / 2 - 20), T("Choose…", "Alege…"), D_KBADD);
		y += RH + 4;
	}
	return y;
}

static int pg_upd(int x, int y, int w)
{
	char s[128];
	card(x, y, w, 110);
	glyph(G_UPDATE, x + 24, y + 30, 48, th.accent);
	text(F_lg, x + 96, y + 26, T("Keep your system up to date", "Ține sistemul la zi"), th.text);
	snprintf(s, sizeof s, "%s %s", T("Last full update:", "Ultima actualizare completă:"), last_upd[0] ? last_upd : T("unknown", "necunoscută"));
	text(F_ui, x + 96, y + 60, s, th.sub);
	const char *l = T("Update now", "Actualizează acum");
	button(x + w - tw(F_ui, l) - 52, y + 39, l, 1, A_BTN, B_UPDATE);
	y += 130;
	card(x, y, w, 80);
	glyph(G_INFO, x + 24, y + 28, 22, th.text);
	text(F_ui, x + 62, y + 18, T("A terminal opens and asks for your password.", "Se deschide un terminal care cere parola."), th.text);
	text(F_sm, x + 62, y + 44, have("flatpak") ? T("System packages (pacman) and Flatpak apps are updated.", "Se actualizează pachetele de sistem (pacman) și aplicațiile Flatpak.")
		: T("System packages (pacman) are updated.", "Se actualizează pachetele de sistem (pacman)."), th.sub);
	return y + 80;
}

/* ---------- drawing ---------- */

static void draw_drop(void)
{
	int h = drop_n * 36 + 8;
	rrect(drop_x - 1, drop_y + 1, drop_w + 2, h + 2, 8, 0x000000, 40);
	rbox(drop_x, drop_y, drop_w, h, 7, th.menu, th.border);
	for (int i = 0; i < drop_n; i++) {
		int iy = drop_y + 4 + i * 36;
		if (i == drop_hover || i == drop_sel) rrect(drop_x + 4, iy, drop_w - 8, 34, 4, th.hover, 255);
		if (i == drop_sel) rrect(drop_x + 4, iy + 10, 3, 14, 1, th.accent, 255);
		textfit(F_ui, drop_x + 16, iy + (34 - fh(F_ui)) / 2, drop_w - 24, drop_opt[i], th.text);
	}
}

static void draw(void)
{
	int cx, cw, y, i;
	nav = ww < 900 ? 64 : NAVW;
	cx = nav + PAD;
	cw = MIN(ww - nav - 2 * PAD, CWMAX);
	char name[128];
	buf_ensure(ww, wh);
	nhot = 0;
	fill(0, 0, ww, wh, th.menu);
	y = HEAD - scroll;
	switch (page) {
	case P_SYS: y = pg_sys(cx, y, cw); break;
	case P_BT: y = pg_bt(cx, y, cw); break;
	case P_NET: y = pg_net(cx, y, cw); break;
	case P_PERS: y = pg_pers(cx, y, cw); break;
	case P_APPS: y = pg_apps(cx, y, cw); break;
	case P_LANG: y = pg_lang(cx, y, cw); break;
	case P_UPD: y = pg_upd(cx, y, cw); break;
	}
	content_h = y + scroll - HEAD + PAD;
	/* page hot spots hidden under the header are not clickable */
	for (i = 0; i < nhot; i++) if (hot[i].y < HEAD) { hot[i].h -= HEAD - hot[i].y; hot[i].y = HEAD; }
	fill(nav, 0, ww - nav, HEAD, th.menu);
	text(F_xl, cx, 26, pg_name(page), th.text);
	if (status_msg[0]) text(F_ui, cx + tw(F_xl, pg_name(page)) + 24, 38, status_msg, th.sub);
	if (content_h > wh - HEAD) {
		int th_ = (wh - HEAD) * (wh - HEAD) / content_h;
		rrect(ww - 8, HEAD + scroll * (wh - HEAD - th_) / MAX(1, content_h - (wh - HEAD)), 3, th_, 1, th.sub, 160);
	}
	fill(0, 0, nav, wh, th.menu);
	user_name(name, sizeof name);
	char ini[8] = {0};
	ini[0] = (char)toupper((unsigned char)name[0]);
	if (nav == NAVW) {
		rrect(20, 24, 64, 64, 32, th.accent, 255);
		textc(F_xl, 20, 24 + (64 - fh(F_xl)) / 2, 64, ini, th.onaccent);
		textfit(F_md, 98, 36, NAVW - 110, name, th.text);
		text(F_sm, 98, 58, T("Local account", "Cont local"), th.sub);
	} else {
		rrect(14, 36, 36, 36, 18, th.accent, 255);
		textc(F_md, 14, 36 + (36 - fh(F_md)) / 2, 36, ini, th.onaccent);
	}
	for (i = 0; i < NPAGE; i++) {
		int ny = 116 + i * 40;
		if (i == page || hov(8, ny, nav - 16, 36)) rrect(8, ny, nav - 16, 36, 5, i == page ? th.hover : mix(th.menu, th.hover, 140), 255);
		if (i == page) rrect(8, ny + 10, 3, 16, 1, th.accent, 255);
		glyph(pg_glyph[i], nav == NAVW ? 24 : 22, ny + 8, 20, th.text);
		if (nav == NAVW) text(F_ui, 58, ny + (36 - fh(F_ui)) / 2, pg_name(i), th.text);
		addhot(8, ny, nav - 16, 36, A_NAV, i);
	}
	if (drop >= 0) draw_drop();
	blit(win, ww, wh);
}

/* ---------- actions ---------- */

static void apply_cfg(int dark_changed)
{
	cfg_save();
	send_reload();
	theme_apply();
	if (dark_changed) theme_export();
	XSetWindowBackground(dpy, win, th.menu);
}

static void open_drop(int id, int x, int y, int w)
{
	drop = id;
	drop_n = 0;
	drop_sel = 0;
	drop_hover = -1;
	if (id == D_MODE) {
		strcpy(drop_opt[drop_n++], T("Light", "Luminos"));
		strcpy(drop_opt[drop_n++], T("Dark", "Întunecat"));
		drop_sel = cfg.dark;
	} else if (id == D_ALIGN) {
		strcpy(drop_opt[drop_n++], T("Center", "Centru"));
		strcpy(drop_opt[drop_n++], T("Left", "Stânga"));
		drop_sel = cfg.align_left;
	} else if (id == D_BAR) {
		strcpy(drop_opt[drop_n++], T("Bottom", "Jos"));
		strcpy(drop_opt[drop_n++], T("Top", "Sus"));
		drop_sel = cfg.bar_top;
	} else if (id == D_LANG) {
		strcpy(drop_opt[drop_n++], "English");
		strcpy(drop_opt[drop_n++], "Română");
		drop_sel = RO;
	} else if (id == D_KBADD) {
		for (int i = 0; i < nkbadd && drop_n < (int)LEN(drop_opt); i++) {
			const Kbd *k = kbd_info(kbadd[i]);
			snprintf(drop_opt[drop_n++], sizeof drop_opt[0], "%s", T(k->en, k->ro));
		}
		drop_sel = -1;
	} else if (id >= D_RES) {
		Out *o = &outs[id - D_RES];
		for (int i = 0; i < o->nm && drop_n < 14; i++) strcpy(drop_opt[drop_n++], o->modes[i]);
		drop_sel = o->cur;
	}
	drop_w = w;
	drop_x = x;
	drop_y = y + 36;
	if (drop_y + drop_n * 36 + 8 > wh) drop_y = MAX(4, y - drop_n * 36 - 12);
}

static void pick_drop(int i)
{
	int id = drop;
	drop = -1;
	if (id == D_MODE && cfg.dark != i) { cfg.dark = i; apply_cfg(1); }
	else if (id == D_ALIGN && cfg.align_left != i) { cfg.align_left = i; apply_cfg(0); }
	else if (id == D_BAR && cfg.bar_top != i) { cfg.bar_top = i; apply_cfg(0); }
	else if (id == D_LANG && RO != i) { RO = i; lang_changed = 1; apply_cfg(0); }
	else if (id == D_KBADD && i < nkbadd && nkb < (int)LEN(kb)) { strcpy(kb[nkb++], kbadd[i]); kb_save(); }
	else if (id >= D_RES && outs[id - D_RES].cur != i) {
		run("xrandr", "--output", outs[id - D_RES].name, "--mode", drop_opt[i], NULL);
		outs[id - D_RES].cur = i;
		later("", 1500);
	}
}

static void slider_set(int id, int x, int final)
{
	int pct = CLAMP((x - sl_x[id]) * 100 / MAX(1, sl_w[id]), 0, 100);
	char a[16];
	if (id == S_BRIGHT) {
		pct = MAX(pct, 1);
		bl_val = pct * bl_max / 100;
		if (final) { snprintf(a, sizeof a, "%d%%", pct); run("brightnessctl", "-q", "set", a, NULL); }
	} else {
		vol = pct;
		if (final) volume_set(pct);
	}
}

static void term_self(const char *mode, const char *arg)
{
	char t[128];
	snprintf(t, sizeof t, "%s", cfg.terminal);
	t[strcspn(t, " ")] = 0;
	run(t, "-e", self, mode, arg, NULL);
}

static void wifi_connect(const char *ssid, const char *pw)
{
	char b[8192], *l, *sv;
	int known = 0;
	if (!pw && run_read("nmcli -t -f NAME con show 2>/dev/null", b, sizeof b))
		for (l = strtok_r(b, "\n", &sv); l; l = strtok_r(NULL, "\n", &sv)) known |= !strcmp(l, ssid);
	if (known) run("nmcli", "con", "up", "id", ssid, NULL);
	else if (pw) run("nmcli", "dev", "wifi", "connect", ssid, "password", pw, NULL);
	else run("nmcli", "dev", "wifi", "connect", ssid, NULL);
	snprintf(b, sizeof b, "%s %s…", T("Connecting to", "Se conectează la"), ssid);
	later(b, 5000);
}

static void click(Hot *h)
{
	switch (h->act) {
	case A_NAV:
		if (page != h->arg) { page = h->arg; scroll = 0; pw_on = 0; status_msg[0] = 0; load_page(); }
		break;
	case A_TOGGLE:
		if (h->arg == T_BT) {
			bt_on = !bt_on;
			if (bt_on) run("sh", "-c", "rfkill unblock bluetooth 2>/dev/null; bluetoothctl power on", NULL);
			else run("bluetoothctl", "power", "off", NULL);
			later("", 1500);
		} else if (h->arg == T_WIFI) {
			wifi_on = !wifi_on;
			run("nmcli", "radio", "wifi", wifi_on ? "on" : "off", NULL);
			later(wifi_on ? T("Searching for networks…", "Se caută rețele…") : "", wifi_on ? 4000 : 800);
		} else if (h->arg == T_TILING) { cfg.tiling = !cfg.tiling; apply_cfg(0); }
		break;
	case A_DROP: open_drop(h->arg, h->x, h->y, h->w); break;
	case A_SLIDER: slide = h->arg; slider_set(slide, mx, 0); break;
	case A_SWATCH: cfg.accent = accents[h->arg]; apply_cfg(0); break;
	case A_WALL: snprintf(cfg.wallpaper, sizeof cfg.wallpaper, "%s", walls[h->arg]); apply_cfg(0); break;
	case A_UNINST: term_self("--uninstall", apps[h->arg].id); break;
	case A_KBDEL:
		if (nkb > 1) { memmove(kb[h->arg], kb[h->arg + 1], (size_t)(nkb - h->arg - 1) * sizeof kb[0]); nkb--; kb_save(); }
		break;
	case A_KBUP: { char t[16]; memcpy(t, kb[h->arg], 16); memmove(kb[1], kb[0], (size_t)h->arg * sizeof kb[0]); memcpy(kb[0], t, 16); kb_save(); break; }
	case A_BTDEV:
		run("bluetoothctl", bts[h->arg].conn ? "disconnect" : "connect", bts[h->arg].mac, NULL);
		later(T("Working…", "Se lucrează…"), 4000);
		break;
	case A_WIFI:
		if (nets[h->arg].inuse) { run("nmcli", "con", "down", "id", nets[h->arg].ssid, NULL); later("", 1500); break; }
		if (nets[h->arg].sec) {
			char b[8192], *l, *sv;
			int known = 0;
			if (run_read("nmcli -t -f NAME con show 2>/dev/null", b, sizeof b))
				for (l = strtok_r(b, "\n", &sv); l; l = strtok_r(NULL, "\n", &sv)) known |= !strcmp(l, nets[h->arg].ssid);
			if (!known) {
				pw_on = 1;
				pw_buf[0] = 0;
				snprintf(pw_ssid, sizeof pw_ssid, "%s", nets[h->arg].ssid);
				break;
			}
		}
		wifi_connect(nets[h->arg].ssid, NULL);
		break;
	case A_BTN:
		switch (h->arg) {
		case B_DISPADV: run("arandr", NULL); break;
		case B_SOUNDADV: run("pavucontrol", NULL); break;
		case B_NETADV: run("nm-connection-editor", NULL); break;
		case B_BTADD:
			if (have("blueman-manager")) run("blueman-manager", NULL);
			else term_self("--bt-pair", "");
			break;
		case B_RESCAN: run("nmcli", "dev", "wifi", "rescan", NULL); later(T("Searching for networks…", "Se caută rețele…"), 4000); break;
		case B_PWCANCEL: pw_on = 0; break;
		case B_PWOK: if (pw_buf[0]) { pw_on = 0; wifi_connect(pw_ssid, pw_buf); memset(pw_buf, 0, sizeof pw_buf); } break;
		case B_UPDATE: term_self("--update", ""); break;
		case B_BROWSE: {
			char out[512] = "";
			if (have("zenity")) run_read("zenity --file-selection --file-filter='*.png *.jpg *.jpeg *.webp' 2>/dev/null", out, sizeof out);
			else run_read("kdialog --getopenfilename ~ 'image/png image/jpeg image/webp' 2>/dev/null", out, sizeof out);
			out[strcspn(out, "\n")] = 0;
			if (out[0]) { snprintf(cfg.wallpaper, sizeof cfg.wallpaper, "%s", out); apply_cfg(0); }
			break;
		}
		}
		break;
	}
}

static void clamp_scroll(void)
{
	scroll = CLAMP(scroll, 0, MAX(0, content_h - (wh - HEAD)));
}

static void on_press(XButtonEvent *e)
{
	mx = e->x; my = e->y;
	if (drop >= 0) {
		int h = drop_n * 36 + 8;
		if (e->button == 1 && e->x >= drop_x && e->x < drop_x + drop_w && e->y >= drop_y + 4 && e->y < drop_y + h - 4)
			pick_drop((e->y - drop_y - 4) / 36);
		else drop = -1;
		draw();
		return;
	}
	if (e->button == 4 || e->button == 5) {
		if (e->x >= nav) { scroll += e->button == 4 ? -60 : 60; clamp_scroll(); draw(); }
		return;
	}
	if (e->button != 1) return;
	for (int i = nhot - 1; i >= 0; i--)
		if (hov(hot[i].x, hot[i].y, hot[i].w, hot[i].h)) { click(&hot[i]); break; }
	clamp_scroll();
	draw();
}

static void on_motion(int x, int y)
{
	int h = -1;
	mx = x; my = y;
	if (slide >= 0) { slider_set(slide, x, 0); draw(); return; }
	if (drop >= 0) {
		int d = (x >= drop_x && x < drop_x + drop_w && y >= drop_y + 4) ? (y - drop_y - 4) / 36 : -1;
		if (d >= drop_n) d = -1;
		if (d != drop_hover) { drop_hover = d; draw(); }
		return;
	}
	for (int i = nhot - 1; i >= 0; i--)
		if (hov(hot[i].x, hot[i].y, hot[i].w, hot[i].h)) { h = i; break; }
	if (h != hover) { hover = h; draw(); }
}

static void on_key(XKeyEvent *e)
{
	char b[32];
	KeySym ks;
	int n = XLookupString(e, b, sizeof b - 1, &ks, NULL);
	if (ks == XK_Escape) { if (drop >= 0) drop = -1; else pw_on = 0; }
	else if (pw_on && (ks == XK_Return || ks == XK_KP_Enter) && pw_buf[0]) {
		pw_on = 0;
		wifi_connect(pw_ssid, pw_buf);
		memset(pw_buf, 0, sizeof pw_buf);
	} else if (pw_on && ks == XK_BackSpace) { size_t l = strlen(pw_buf); if (l) pw_buf[l - 1] = 0; }
	else if (pw_on && n > 0 && (unsigned char)b[0] >= 32) {
		b[n] = 0;
		if (strlen(pw_buf) + (size_t)n < sizeof pw_buf) strcat(pw_buf, b);
	} else if ((ks == XK_w || ks == XK_q) && (e->state & ControlMask)) exit(0);
	else if (ks == XK_Page_Down || ks == XK_Down) { scroll += ks == XK_Down ? 60 : wh - HEAD; clamp_scroll(); }
	else if (ks == XK_Page_Up || ks == XK_Up) { scroll -= ks == XK_Up ? 60 : wh - HEAD; clamp_scroll(); }
	draw();
}

/* ---------- terminal helpers (run inside a terminal) ---------- */

static void press_enter(void)
{
	int c;
	printf("\n%s", T("Press Enter to close.", "Apăsați Enter pentru a închide."));
	fflush(stdout);
	while ((c = getchar()) != '\n' && c != EOF);
}

static int uninstall(const char *id)
{
	const char *dirs[] = {"/usr/share/applications", "/usr/local/share/applications", "~/.local/share/applications",
		"/var/lib/flatpak/exports/share/applications", "~/.local/share/flatpak/exports/share/applications"};
	char p[700] = "", cmd[900], pkg[256] = "";
	struct stat st;
	if (strchr(id, '\'') || strchr(id, '/')) return 1;
	for (size_t i = 0; i < LEN(dirs) && !p[0]; i++) {
		if (dirs[i][0] == '~') snprintf(p, sizeof p, "%s%s/%s", home(), dirs[i] + 1, id);
		else snprintf(p, sizeof p, "%s/%s", dirs[i], id);
		if (stat(p, &st)) p[0] = 0;
	}
	if (!p[0]) printf("%s %s\n", T("Not found:", "Nu am găsit:"), id);
	else if (strstr(p, "flatpak")) {
		snprintf(pkg, sizeof pkg, "%s", id);
		if (strstr(pkg, ".desktop")) *strstr(pkg, ".desktop") = 0;
		snprintf(cmd, sizeof cmd, "flatpak uninstall '%s'", pkg);
		if (system(cmd)) {}
	} else {
		char rp[PATH_MAX];
		snprintf(cmd, sizeof cmd, "pacman -Qqo -- '%s' 2>/dev/null", realpath(p, rp) ? rp : p);
		run_read(cmd, pkg, sizeof pkg);
		pkg[strcspn(pkg, "\n")] = 0;
		if (!pkg[0] || strchr(pkg, '\'')) printf("%s\n%s\n", T("This app is not from a system package:", "Aplicația nu vine dintr-un pachet de sistem:"), p);
		else {
			printf("%s %s\n\n", T("Removing package", "Se dezinstalează pachetul"), pkg);
			snprintf(cmd, sizeof cmd, "sudo pacman -Rns '%s'", pkg);
			if (system(cmd)) {}
		}
	}
	press_enter();
	return 0;
}

static int update(void)
{
	printf("%s\n\n", T("Updating system packages…", "Se actualizează pachetele de sistem…"));
	if (system("sudo pacman -Syu")) {}
	if (have("flatpak")) { printf("\n%s\n\n", T("Updating Flatpak apps…", "Se actualizează aplicațiile Flatpak…")); if (system("flatpak update -y")) {} }
	press_enter();
	return 0;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
	XSizeHints sh = {0};
	XClassHint ch = {"gluewin-settings", "Gluewin-settings"};
	XSetWindowAttributes swa;
	XEvent ev;
	setlocale(LC_ALL, "");
	cfg_load();
	if (argc > 2 && !strcmp(argv[1], "--uninstall")) return uninstall(argv[2]);
	if (argc > 1 && !strcmp(argv[1], "--update")) return update();
	if (argc > 1 && !strcmp(argv[1], "--bt-pair")) {
		printf("%s\n\n", T("Put the device in pairing mode, then type: scan on / pair MAC / connect MAC / quit",
			"Pune dispozitivul în modul de asociere, apoi scrie: scan on / pair MAC / connect MAC / quit"));
		return system("bluetoothctl") != 0;
	}
	for (int i = 1; i + 1 < argc; i++)
		if (!strcmp(argv[i], "--page"))
			for (int p = 0; p < NPAGE; p++) {
				if (!strcmp(argv[i + 1], pg_id[p])) page = p;
				if (!strcmp(argv[i + 1], "display") || !strcmp(argv[i + 1], "sound")) page = P_SYS;
				if (!strcmp(argv[i + 1], "wifi")) page = P_NET;
				if (!strcmp(argv[i + 1], "keyboard") || !strcmp(argv[i + 1], "time")) page = P_LANG;
			}
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	self[n > 0 ? n : 0] = 0;
	if (!self[0]) strcpy(self, "gluewin-settings");
	signal(SIGCHLD, SIG_IGN);
	if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "gluewin-settings: cannot open display\n"); return 1; }
	gfx_init();
	cfg_load();
	theme_apply();
	fonts_init();
	swa.background_pixel = th.menu;
	swa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | StructureNotifyMask | LeaveWindowMask;
	win = XCreateWindow(dpy, root, 0, 0, ww, wh, 0, depth, InputOutput, vis, CWBackPixel | CWEventMask, &swa);
	sh.flags = PMinSize;
	sh.min_width = 760;
	sh.min_height = 480;
	XSetWMNormalHints(dpy, win, &sh);
	XSetClassHint(dpy, win, &ch);
	const char *title = T("Settings", "Setări");
	XStoreName(dpy, win, title);
	XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_NAME", False), XInternAtom(dpy, "UTF8_STRING", False), 8,
		PropModeReplace, (const unsigned char *)title, (int)strlen(title));
	WM_DEL = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &WM_DEL, 1);
	load_page();
	XMapWindow(dpy, win);
	for (;;) {
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			switch (ev.type) {
			case Expose: if (!ev.xexpose.count) draw(); break;
			case ConfigureNotify:
				if (ev.xconfigure.width != ww || ev.xconfigure.height != wh) {
					ww = ev.xconfigure.width; wh = ev.xconfigure.height;
					clamp_scroll();
				}
				break;
			case ButtonPress: on_press(&ev.xbutton); break;
			case ButtonRelease:
				if (slide >= 0 && ev.xbutton.button == 1) { slider_set(slide, ev.xbutton.x, 1); slide = -1; draw(); }
				break;
			case MotionNotify:
				while (XCheckTypedWindowEvent(dpy, win, MotionNotify, &ev));
				on_motion(ev.xmotion.x, ev.xmotion.y);
				break;
			case LeaveNotify: if (slide < 0) on_motion(-1, -1); break;
			case KeyPress: on_key(&ev.xkey); break;
			case ClientMessage:
				if ((Atom)ev.xclient.data.l[0] == WM_DEL) return 0;
				break;
			}
		}
		fd_set fds;
		struct timeval tv = {0, 250000};
		FD_ZERO(&fds);
		FD_SET(ConnectionNumber(dpy), &fds);
		select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, refresh_at ? &tv : NULL);
		if (refresh_at && now_ms() >= refresh_at) {
			refresh_at = 0;
			status_msg[0] = 0;
			load_page();
			draw();
		}
	}
}
