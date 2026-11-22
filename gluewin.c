#include "common.h"

enum { TH = 32, BW = 1, BTNW = 46, TBH = 48, HDL = 6, MAXMON = 8, RAD = 8, NWS = 9, GAP = 6 };
enum { DL = 1, DR = 2, DT = 4, DB = 8 };

typedef struct Client Client;
struct Client {
	Window win, frame, hdl[4];
	int x, y, w, h, ox, oy, ow, oh, fx, fy, fw, fh, gx, gy, gw, gh, tiled;
	int deco, dialog, minimized, max, full, snap, skiptb, noinput, takefocus, delwin;
	int minw, minh, ignore_unmap, hover, press, shw, shh, shr, ws, floating, tord;
	unsigned long stamp;
	char title[256], cls[96], inst[96];
	Icon ic;
	int hasicon;
	App *app;
	Client *next;
};

typedef struct { int x, y, w, h; Window bar; } Mon;
typedef struct { App *app; char key[96]; Client *w[32]; int n, pinned; } Group;
typedef struct { int kind, x, w, g; } TbItem;
typedef struct { int x, y, w, h, kind, idx; } Hit;
typedef struct { char label[160]; int id; } MItem;

enum {
	WMProtocols, WMDelete, WMTakeFocus, WMState, WMChangeState, NetSupported, NetWMName, NetWMIcon,
	NetWMState, NetWMFullscreen, NetWMHidden, NetWMMaxV, NetWMMaxH, NetWMSkipTaskbar, NetActiveWindow,
	NetClientList, NetClientListStacking, NetCheck, NetWMType, NetTypeDialog, NetTypeDock, NetTypeDesktop,
	NetTypeSplash, NetTypeUtility, NetTypeNotification, NetCloseWindow, NetMoveResize, NetFrameExtents,
	NetReqFrameExtents, NetWorkarea, NetNumDesktops, NetCurDesktop, NetWMDesktop, MotifHints, Utf8,
	TrayOpcode, TrayOrient, Manager, Xembed, NATOMS
};
static const char *atomnames[NATOMS] = {
	"WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "WM_STATE", "WM_CHANGE_STATE", "_NET_SUPPORTED",
	"_NET_WM_NAME", "_NET_WM_ICON", "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_HIDDEN",
	"_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_SKIP_TASKBAR",
	"_NET_ACTIVE_WINDOW", "_NET_CLIENT_LIST", "_NET_CLIENT_LIST_STACKING", "_NET_SUPPORTING_WM_CHECK",
	"_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_WINDOW_TYPE_DOCK",
	"_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_UTILITY",
	"_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_CLOSE_WINDOW", "_NET_WM_MOVERESIZE", "_NET_FRAME_EXTENTS",
	"_NET_REQUEST_FRAME_EXTENTS", "_NET_WORKAREA", "_NET_NUMBER_OF_DESKTOPS", "_NET_CURRENT_DESKTOP",
	"_NET_WM_DESKTOP", "_MOTIF_WM_HINTS", "UTF8_STRING", "_NET_SYSTEM_TRAY_OPCODE",
	"_NET_SYSTEM_TRAY_ORIENTATION", "MANAGER", "_XEMBED"
};
static Atom A[NATOMS], A_tray;

enum { CUR_NORMAL, CUR_L, CUR_R, CUR_T, CUR_B, CUR_TL, CUR_TR, CUR_BL, CUR_BR, CUR_MOVE, NCUR };
static Cursor cur[NCUR];

static Client *clients, *sel;
static Client *zorder[512];
static int nz;
static unsigned long stampctr;
static int curws, tilews[NWS], last_tiling = -1, tordctr;
static Mon mons[MAXMON];
static int nmons;
static Window wmcheck, docks[32], trayicons[32], trayselwin;
static int ndocks, ntray, running = 1, super_alone, tray_x = -1;
static volatile sig_atomic_t quit_sig;
static Group G[64];
static int ng;
static TbItem tbi[96];
static int ntbi, tb_hmon = -1, tb_hover = -1;

static struct { int on, mode, dir, px, py, ox, oy, ow, oh, moved, zone; Client *c; } drag;
static Window snapwin;

/* popups */
static int grabbed;
static Window smwin, ctxwin, qswin, atwin;
static int sm_on, ctx_on, qs_on, at_on, tv_on, tv_hover = -1, tv_n, tv_ws[NWS + 1], tv_w, tv_h;
static Window tvwin;
static int sm_mode, sm_sel, sm_scroll, sm_hover = -1, sm_mon, SMW = 640, SMH = 700, sm_x, sm_y;
static char sm_q[128];
static int sm_res[1024], sm_nres;
static Hit smh[256];
static int nsmh;
static MItem mi[40];
static int nmi, ctx_hover = -1, ctx_w, ctx_h, ctx_kind, ctx_x, ctx_y;
static App *ctx_app;
static Window ctx_wins[24], ctx_win1;
static int qs_wifi, qs_bt, qs_vol = -1, qs_muted, qs_bri = -1, qs_brimax, qs_drag, qs_hover = -1, QSW = 360, qs_h;
static char qs_bldev[64];
static long long qs_last;
static Client *at_list[64];
static int at_n, at_sel;
static Client *sd_list[256];
static int sd_n;
static Time last_click;
static Client *last_click_c;

enum { TB_START, TB_SEARCH, TB_TASKVIEW, TB_GROUP, TB_TRAY, TB_SYS, TB_LANG, TB_CLOCK, TB_DESK };
enum { H_APP, H_ALL, H_BACK, H_POWER, H_SETTINGS, H_FILES, H_RUN, H_SEARCH };
enum { CX_GROUP = 1, CX_STARTAPP, CX_POWER, CX_DESK, CX_TASKBAR, CX_WIN, CX_CAD, CX_LANG };
enum { QS_WIFI, QS_BT, QS_DARK, QS_BRI, QS_VOL, QS_GEAR };

static void restack(void);
static void tb_draw_all(void);
static void focus(Client *c);
static void close_popups(void);
static void sm_draw(void);
static void apply(Client *c);
static void arrange(void);
static void ws_view(int n);

static int xerror(Display *d, XErrorEvent *e)
{
	(void)d;
	if (e->error_code == BadWindow || e->error_code == BadMatch || e->error_code == BadDrawable ||
		e->error_code == BadAccess || e->error_code == BadValue)
		return 0;
	fprintf(stderr, "gluewin: X error %d request %d\n", e->error_code, e->request_code);
	return 0;
}

static int xerror_start(Display *d, XErrorEvent *e)
{
	(void)d; (void)e;
	fprintf(stderr, "gluewin: another window manager is running\n");
	exit(1);
}

static long long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ---------- lookups ---------- */

static Client *bywin(Window w)
{
	for (Client *c = clients; c; c = c->next) if (c->win == w) return c;
	return NULL;
}

static Client *byframe(Window w)
{
	for (Client *c = clients; c; c = c->next) if (c->frame == w) return c;
	return NULL;
}

static Client *byhandle(Window w, int *side)
{
	for (Client *c = clients; c; c = c->next)
		for (int i = 0; i < 4; i++) if (c->hdl[i] == w) { *side = i; return c; }
	return NULL;
}

static int monbar(Window w)
{
	for (int i = 0; i < nmons; i++) if (mons[i].bar == w) return i;
	return -1;
}

static int mon_at(int x, int y)
{
	for (int i = 0; i < nmons; i++)
		if (x >= mons[i].x && x < mons[i].x + mons[i].w && y >= mons[i].y && y < mons[i].y + mons[i].h) return i;
	return 0;
}

static int mon_of(Client *c) { return mon_at(c->x + c->w / 2, c->y + MIN(c->h / 2, 20)); }

static int pointer_mon(void)
{
	Window r, ch;
	int x, y, wx, wy;
	unsigned m;
	XQueryPointer(dpy, root, &r, &ch, &x, &y, &wx, &wy, &m);
	return mon_at(x, y);
}

static void workarea(int m, int *x, int *y, int *w, int *h)
{
	*x = mons[m].x; *y = mons[m].y + (cfg.bar_top ? TBH : 0); *w = mons[m].w; *h = mons[m].h - TBH;
}

/* ---------- properties ---------- */

static Atom get_atom(Window w, Atom prop)
{
	Atom t, r = None;
	int f;
	unsigned long n, after;
	unsigned char *p = NULL;
	if (XGetWindowProperty(dpy, w, prop, 0, 1, False, XA_ATOM, &t, &f, &n, &after, &p) == Success && p) {
		if (n) r = *(Atom *)p;
		XFree(p);
	}
	return r;
}

static int has_state(Window w, Atom st)
{
	Atom t;
	int f, r = 0;
	unsigned long n, after;
	unsigned char *p = NULL;
	if (XGetWindowProperty(dpy, w, A[NetWMState], 0, 32, False, XA_ATOM, &t, &f, &n, &after, &p) == Success && p) {
		for (unsigned long i = 0; i < n; i++) if (((Atom *)p)[i] == st) r = 1;
		XFree(p);
	}
	return r;
}

static void get_title(Client *c)
{
	XTextProperty tp;
	char **list;
	int n;
	c->title[0] = 0;
	if (!XGetTextProperty(dpy, c->win, &tp, A[NetWMName]) || !tp.nitems)
		if (!XGetTextProperty(dpy, c->win, &tp, XA_WM_NAME) || !tp.nitems) return;
	if (tp.encoding == XA_STRING || tp.encoding == A[Utf8]) snprintf(c->title, sizeof c->title, "%s", (char *)tp.value);
	else if (Xutf8TextPropertyToTextList(dpy, &tp, &list, &n) >= Success && n > 0 && *list) {
		snprintf(c->title, sizeof c->title, "%s", *list);
		XFreeStringList(list);
	}
	XFree(tp.value);
}

static void get_icon(Client *c)
{
	Atom t;
	int f, bw = 0, bh = 0;
	unsigned long n, after, i, best = 0;
	unsigned char *p = NULL;
	long *d;
	c->hasicon = 0;
	if (XGetWindowProperty(dpy, c->win, A[NetWMIcon], 0, 1 << 20, False, XA_CARDINAL, &t, &f, &n, &after, &p) != Success || !p)
		return;
	d = (long *)p;
	for (i = 0; i + 2 <= n;) {
		int w = (int)d[i], h = (int)d[i + 1];
		if (w <= 0 || h <= 0 || i + 2 + (unsigned long)w * h > n) break;
		if (!bw || (bw < 32 && w > bw) || (w >= 32 && w < bw)) { bw = w; bh = h; best = i; }
		i += 2 + (unsigned long)w * h;
	}
	if (bw) {
		unsigned *px = malloc((size_t)bw * bh * 4);
		for (i = 0; i < (unsigned long)bw * bh; i++) px[i] = (unsigned)d[best + 2 + i];
		icon_set(&c->ic, px, bw, bh);
		free(px);
		c->hasicon = 1;
	}
	XFree(p);
}

static void get_class(Client *c)
{
	XClassHint ch = {0};
	c->cls[0] = c->inst[0] = 0;
	if (XGetClassHint(dpy, c->win, &ch)) {
		if (ch.res_class) snprintf(c->cls, sizeof c->cls, "%s", ch.res_class);
		if (ch.res_name) snprintf(c->inst, sizeof c->inst, "%s", ch.res_name);
		if (ch.res_class) XFree(ch.res_class);
		if (ch.res_name) XFree(ch.res_name);
	}
}

static void get_hints(Client *c)
{
	XWMHints *wh = XGetWMHints(dpy, c->win);
	XSizeHints sh;
	long supplied;
	Atom *prot;
	int n;
	c->noinput = 0;
	if (wh) {
		if ((wh->flags & InputHint) && !wh->input) c->noinput = 1;
		XFree(wh);
	}
	c->minw = 120; c->minh = 40;
	if (XGetWMNormalHints(dpy, c->win, &sh, &supplied) && (sh.flags & PMinSize)) {
		c->minw = MAX(c->minw, sh.min_width);
		c->minh = MAX(c->minh, sh.min_height);
	}
	c->takefocus = c->delwin = 0;
	if (XGetWMProtocols(dpy, c->win, &prot, &n)) {
		while (n--) {
			if (prot[n] == A[WMTakeFocus]) c->takefocus = 1;
			if (prot[n] == A[WMDelete]) c->delwin = 1;
		}
		XFree(prot);
	}
}

static int motif_nodeco(Window w)
{
	Atom t;
	int f, r = 0;
	unsigned long n, after;
	unsigned char *p = NULL;
	if (XGetWindowProperty(dpy, w, A[MotifHints], 0, 5, False, AnyPropertyType, &t, &f, &n, &after, &p) == Success && p) {
		long *h = (long *)p;
		if (n >= 3 && (h[0] & 2) && h[2] == 0) r = 1;
		XFree(p);
	}
	return r;
}

static void set_wm_state(Client *c, long st)
{
	long d[2] = {st, None};
	XChangeProperty(dpy, c->win, A[WMState], A[WMState], 32, PropModeReplace, (unsigned char *)d, 2);
}

static void ewmh_state(Client *c)
{
	Atom s[4];
	int n = 0;
	if (c->full) s[n++] = A[NetWMFullscreen];
	if (c->minimized) s[n++] = A[NetWMHidden];
	if (c->max) { s[n++] = A[NetWMMaxV]; s[n++] = A[NetWMMaxH]; }
	XChangeProperty(dpy, c->win, A[NetWMState], XA_ATOM, 32, PropModeReplace, (unsigned char *)s, n);
}

static void ewmh_lists(void)
{
	Window w[512];
	int n = 0;
	for (Client *c = clients; c && n < 512; c = c->next) w[n++] = c->win;
	XChangeProperty(dpy, root, A[NetClientList], XA_WINDOW, 32, PropModeReplace, (unsigned char *)w, n);
	for (n = 0; n < nz; n++) w[n] = zorder[n]->win;
	XChangeProperty(dpy, root, A[NetClientListStacking], XA_WINDOW, 32, PropModeReplace, (unsigned char *)w, n);
}

static void ewmh_workarea(void)
{
	long wa[4];
	int x, y, w, h;
	workarea(0, &x, &y, &w, &h);
	wa[0] = x; wa[1] = y; wa[2] = w; wa[3] = h;
	XChangeProperty(dpy, root, A[NetWorkarea], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)wa, 4);
}

/* ---------- client geometry ---------- */

static int c_th(Client *c) { return c->deco && !c->full ? TH : 0; }
static int c_bw(Client *c) { return c->deco && !c->full && !c->max ? BW : 0; }
static int resizable(Client *c) { return !c->full && !c->max && !c->minimized; }

static int is_tiled(Client *c)
{
	return tilews[c->ws] && !c->floating && !c->dialog && c->deco && !c->full && !c->max && !c->snap &&
		!c->minimized && !c->skiptb;
}

static int pop_y(int m, int h)
{
	return cfg.bar_top ? mons[m].y + TBH + 12 : mons[m].y + mons[m].h - TBH - 12 - h;
}

static void send_configure(Client *c)
{
	XConfigureEvent ce = {0};
	int bw = c_bw(c), tt = c_th(c);
	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = ce.window = c->win;
	ce.x = c->x + bw;
	ce.y = c->y + tt;
	ce.width = MAX(c->w - 2 * bw, 1);
	ce.height = MAX(c->h - tt - bw, 1);
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

static int btn_at(Client *c, int x, int y)
{
	if (!c->deco || c->full || y >= TH) return 0;
	int n = c->dialog ? 1 : 3, i = (c->w - x) / BTNW;
	if (x >= c->w || i >= n) return 0;
	return 3 - i;
}

static void draw_frame(Client *c)
{
	int w = c->w, act = c == sel, bx, i, n, id;
	unsigned bg = act ? th.title : th.title_in, fg = act ? th.text : th.sub;
	if (!c->deco || c->full || c->minimized) return;
	buf_ensure(w, TH);
	fill(0, 0, w, TH, bg);
	if (!c->max) { fill(0, 0, w, 1, th.border); fill(0, 0, 1, TH, th.border); fill(w - 1, 0, 1, TH, th.border); }
	if (c->hasicon) icon_draw(&c->ic, 10, (TH - 16) / 2, 16);
	else if (c->app && app_icon(c->app)) icon_draw(&c->app->ic, 10, (TH - 16) / 2, 16);
	else letter_tile(c->title[0] ? c->title : c->cls, 10, (TH - 16) / 2, 16);
	n = c->dialog ? 1 : 3;
	textfit(F_sm, 34, (TH - fh(F_sm)) / 2, w - 34 - n * BTNW - 8, c->title[0] ? c->title : c->cls, fg);
	for (i = 0; i < n; i++) {
		int b = 3 - i;
		bx = w - (i + 1) * BTNW;
		id = b == 3 ? G_CLOSE : b == 1 ? G_MIN : c->max || c->snap ? G_RESTORE : G_MAX;
		unsigned gc = fg;
		if (c->hover == b) {
			if (b == 3) { fill(bx, c->max ? 0 : 1, BTNW - (c->max ? 0 : 1), TH - (c->max ? 0 : 1), c->press == b ? 0x9B2316 : 0xC42B1C); gc = 0xFFFFFF; }
			else fill(bx, c->max ? 0 : 1, BTNW, TH - (c->max ? 0 : 1), c->press == b ? th.press : th.hover);
		}
		glyph(id, bx + (BTNW - 24) / 2, (TH - 24) / 2, 24, gc);
	}
	XCopyArea(dpy, B.pm, c->frame, DefaultGC(dpy, scr), 0, 0, w, TH, 0, 0);
}

static void place_handles(Client *c)
{
	if (!resizable(c) || !c->deco) {
		for (int i = 0; i < 4; i++) XUnmapWindow(dpy, c->hdl[i]);
		return;
	}
	XMoveResizeWindow(dpy, c->hdl[0], c->x - HDL, c->y - HDL, c->w + 2 * HDL, HDL);
	XMoveResizeWindow(dpy, c->hdl[1], c->x - HDL, c->y + c->h, c->w + 2 * HDL, HDL);
	XMoveResizeWindow(dpy, c->hdl[2], c->x - HDL, c->y, HDL, c->h);
	XMoveResizeWindow(dpy, c->hdl[3], c->x + c->w, c->y, HDL, c->h);
	for (int i = 0; i < 4; i++) XMapWindow(dpy, c->hdl[i]);
}

static void apply(Client *c)
{
	int bw = c_bw(c), tt = c_th(c), r;
	c->w = MAX(c->w, 2 * bw + 1);
	c->h = MAX(c->h, tt + bw + 1);
	XSetWindowBackground(dpy, c->frame, c->max || c->full ? th.title : th.border);
	XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
	XMoveResizeWindow(dpy, c->win, bw, tt, MAX(c->w - 2 * bw, 1), MAX(c->h - tt - bw, 1));
	r = c->deco && !c->max && !c->full && !c->snap ? RAD : 0;
	if (c->shw != c->w || c->shh != c->h || c->shr != r) {
		shape_round(c->frame, c->w, c->h, r);
		c->shw = c->w; c->shh = c->h; c->shr = r;
	}
	if (!c->minimized) place_handles(c);
	send_configure(c);
	draw_frame(c);
	long ext[4] = {bw, bw, tt, bw};
	XChangeProperty(dpy, c->win, A[NetFrameExtents], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)ext, 4);
}

static void move_only(Client *c)
{
	XMoveWindow(dpy, c->frame, c->x, c->y);
	place_handles(c);
	send_configure(c);
}

static void save_geom(Client *c)
{
	if (!c->max && !c->snap && !c->full) { c->ox = c->x; c->oy = c->y; c->ow = c->w; c->oh = c->h; }
}

static void set_max(Client *c, int on)
{
	int x, y, w, h;
	if (c->full) return;
	if (on) {
		save_geom(c);
		workarea(mon_of(c), &x, &y, &w, &h);
		c->max = 1; c->snap = 0;
		c->x = x; c->y = y; c->w = w; c->h = h;
	} else if (c->max || c->snap) {
		c->max = c->snap = 0;
		c->x = c->ox; c->y = c->oy; c->w = c->ow; c->h = c->oh;
	}
	apply(c);
	ewmh_state(c);
	arrange();
}

static void set_snap(Client *c, int side)
{
	int x, y, w, h, m = mon_of(c);
	if (c->full) return;
	save_geom(c);
	workarea(m, &x, &y, &w, &h);
	c->max = 0;
	c->snap = side;
	c->x = side == 1 ? x : x + w / 2;
	c->y = y;
	c->w = side == 1 ? w / 2 : w - w / 2;
	c->h = h;
	apply(c);
	ewmh_state(c);
}

static void set_full(Client *c, int on)
{
	int m = mon_of(c);
	if (on == c->full) return;
	if (on) {
		c->fx = c->x; c->fy = c->y; c->fw = c->w; c->fh = c->h;
		c->full = 1;
		c->x = mons[m].x; c->y = mons[m].y; c->w = mons[m].w; c->h = mons[m].h;
	} else {
		c->full = 0;
		c->x = c->fx; c->y = c->fy; c->w = c->fw; c->h = c->fh;
	}
	apply(c);
	ewmh_state(c);
	arrange();
	restack();
}

static int tord_cmp(const void *a, const void *b)
{
	return (*(Client *const *)a)->tord - (*(Client *const *)b)->tord;
}

static int tiled_on(int m, Client **t, int max)
{
	int n = 0;
	for (Client *c = clients; c && n < max; c = c->next)
		if (c->ws == curws && is_tiled(c) && mon_of(c) == m) t[n++] = c;
	qsort(t, (size_t)n, sizeof *t, tord_cmp);
	return n;
}

static void arrange(void)
{
	Client *t[64];
	int m, n, i, x, y, w, h, mw, sh;
	/* windows leaving the tiling get their floating size and place back */
	for (Client *c = clients; c; c = c->next)
		if (c->tiled && !is_tiled(c) && !c->max && !c->full && !c->snap && !c->minimized) {
			c->tiled = 0;
			c->x = c->gx; c->y = c->gy; c->w = c->gw; c->h = c->gh;
			apply(c);
		}
	if (!tilews[curws]) return;
	for (m = 0; m < nmons; m++) {
		if (!(n = tiled_on(m, t, 64))) continue;
		workarea(m, &x, &y, &w, &h);
		x += GAP; y += GAP; w -= 2 * GAP; h -= 2 * GAP;
		mw = n > 1 ? w * 55 / 100 : w;
		for (i = 0; i < n; i++) {
			Client *c = t[i];
			int nx, ny, nw, nh;
			if (!c->tiled) { c->tiled = 1; c->gx = c->x; c->gy = c->y; c->gw = c->w; c->gh = c->h; }
			if (!i) { nx = x; ny = y; nw = n > 1 ? mw - GAP / 2 : w; nh = h; }
			else {
				sh = (h - GAP * (n - 2)) / (n - 1);
				nx = x + mw + GAP / 2; nw = w - mw - GAP / 2;
				ny = y + (i - 1) * (sh + GAP);
				nh = i == n - 1 ? y + h - ny : sh;
			}
			if (c->x != nx || c->y != ny || c->w != nw || c->h != nh) {
				c->x = nx; c->y = ny; c->w = nw; c->h = nh;
				apply(c);
			}
		}
	}
}

static void tile_swap(Client *c, int dir)
{
	Client *t[64];
	int n = tiled_on(mon_of(c), t, 64), i, k;
	for (i = 0; i < n && t[i] != c; i++);
	k = i + dir;
	if (i == n || k < 0 || k >= n) return;
	int o = c->tord; c->tord = t[k]->tord; t[k]->tord = o;
	arrange();
}

/* ---------- stacking & focus ---------- */

static void z_remove(Client *c)
{
	for (int i = 0; i < nz; i++)
		if (zorder[i] == c) { memmove(&zorder[i], &zorder[i + 1], sizeof *zorder * (size_t)(nz - i - 1)); nz--; return; }
}

static void raise_client(Client *c)
{
	z_remove(c);
	if (nz < (int)LEN(zorder)) zorder[nz++] = c;
	restack();
}

static void restack(void)
{
	Window w[2048];
	int n = 0, i, k;
	if (sel && sel->full && !sel->minimized) w[n++] = sel->frame;
	if (sm_on) w[n++] = smwin;
	if (ctx_on) w[n++] = ctxwin;
	if (qs_on) w[n++] = qswin;
	if (at_on) w[n++] = atwin;
	if (tv_on) w[n++] = tvwin;
	if (drag.on && snapwin) w[n++] = snapwin;
	for (i = 0; i < nmons; i++) w[n++] = mons[i].bar;
	for (i = 0; i < ndocks; i++) w[n++] = docks[i];
	for (i = nz - 1; i >= 0 && n < 2040; i--) {
		Client *c = zorder[i];
		if (c == sel && c->full) continue;
		w[n++] = c->frame;
		for (k = 0; k < 4; k++) w[n++] = c->hdl[k];
	}
	if (sel && sel->full && !sel->minimized) {
		/* popups opened over a fullscreen window stay on top of it */
		memmove(w, w + 1, sizeof *w * (size_t)(n - 1));
		k = (sm_on + ctx_on + qs_on + at_on + tv_on);
		memmove(w + k + 1, w + k, sizeof *w * (size_t)(n - 1 - k));
		w[k] = sel->frame;
	}
	if (n) {
		XRaiseWindow(dpy, w[0]);
		XRestackWindows(dpy, w, n);
	}
	ewmh_lists();
}

static void grab_buttons(Client *c, int focused)
{
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if (!focused)
		XGrabButton(dpy, AnyButton, AnyModifier, c->win, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
}

static void send_proto(Client *c, Atom proto)
{
	XEvent ev = {0};
	ev.type = ClientMessage;
	ev.xclient.window = c->win;
	ev.xclient.message_type = A[WMProtocols];
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long)proto;
	ev.xclient.data.l[1] = CurrentTime;
	XSendEvent(dpy, c->win, False, NoEventMask, &ev);
}

static void focus(Client *c)
{
	Client *old = sel;
	if (c && c->minimized) c = NULL;
	sel = c;
	if (old && old != c) { grab_buttons(old, 0); draw_frame(old); }
	if (c) {
		c->stamp = ++stampctr;
		grab_buttons(c, 1);
		if (!c->noinput) XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		if (c->takefocus) send_proto(c, A[WMTakeFocus]);
		XChangeProperty(dpy, root, A[NetActiveWindow], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&c->win, 1);
		draw_frame(c);
	} else {
		Window none = None;
		XSetInputFocus(dpy, wmcheck, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, A[NetActiveWindow], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&none, 1);
	}
	if (old != c && ((old && old->full) || (c && c->full))) restack();
	tb_draw_all();
}

static void focus_top(void)
{
	for (int i = nz - 1; i >= 0; i--)
		if (!zorder[i]->minimized && zorder[i]->ws == curws) { focus(zorder[i]); return; }
	focus(NULL);
}

static void minimize(Client *c)
{
	if (c->minimized) return;
	c->minimized = 1;
	XUnmapWindow(dpy, c->frame);
	for (int i = 0; i < 4; i++) XUnmapWindow(dpy, c->hdl[i]);
	set_wm_state(c, IconicState);
	ewmh_state(c);
	arrange();
	if (sel == c) focus_top();
	else tb_draw_all();
}

static void activate(Client *c)
{
	if (!c) return;
	if (c->ws != curws) ws_view(c->ws);
	if (c->minimized) {
		c->minimized = 0;
		XMapWindow(dpy, c->frame);
		place_handles(c);
		set_wm_state(c, NormalState);
		ewmh_state(c);
		arrange();
	}
	raise_client(c);
	focus(c);
}

static void close_client(Client *c)
{
	if (c->delwin) send_proto(c, A[WMDelete]);
	else XKillClient(dpy, c->win);
}

/* ---------- manage ---------- */

static void manage(Window w, XWindowAttributes *wa)
{
	Client *c, **pp, *par;
	Window trans = None;
	Atom type = get_atom(w, A[NetWMType]);
	XSetWindowAttributes swa;
	int m, x, y, ww, wh, bw, tt;

	if (type == A[NetTypeDock] || type == A[NetTypeDesktop]) {
		XMapWindow(dpy, w);
		XSelectInput(dpy, w, StructureNotifyMask);
		if (type == A[NetTypeDock] && ndocks < (int)LEN(docks)) docks[ndocks++] = w;
		else XLowerWindow(dpy, w);
		restack();
		return;
	}
	c = calloc(1, sizeof *c);
	c->win = w;
	c->ws = curws;
	c->tord = ++tordctr;
	get_title(c);
	get_class(c);
	get_hints(c);
	get_icon(c);
	c->app = app_for_class(c->cls, c->inst);
	c->deco = !motif_nodeco(w) && type != A[NetTypeSplash] && type != A[NetTypeNotification];
	c->dialog = type == A[NetTypeDialog] || type == A[NetTypeUtility] || (XGetTransientForHint(dpy, w, &trans) && trans);
	c->skiptb = has_state(w, A[NetWMSkipTaskbar]) || type == A[NetTypeSplash] || type == A[NetTypeNotification];

	bw = c_bw(c); tt = c_th(c);
	c->w = wa->width + 2 * bw;
	c->h = wa->height + tt + bw;
	par = trans ? bywin(trans) : NULL;
	m = par ? mon_of(par) : pointer_mon();
	workarea(m, &x, &y, &ww, &wh);
	if (c->w > ww) c->w = ww;
	if (c->h > wh) c->h = wh;
	{
		XSizeHints sh;
		long sup;
		int user_pos = XGetWMNormalHints(dpy, w, &sh, &sup) && (sh.flags & (USPosition | PPosition)) && (wa->x || wa->y);
		if (par) { c->x = par->x + (par->w - c->w) / 2; c->y = par->y + (par->h - c->h) / 2; }
		else if (user_pos) { c->x = wa->x - bw; c->y = wa->y - tt; }
		else {
			static int cascade;
			c->x = x + (ww - c->w) / 2 + cascade;
			c->y = y + (wh - c->h) / 2 + cascade;
			cascade = (cascade + 32) % 160;
		}
	}
	c->x = CLAMP(c->x, x, MAX(x, x + ww - c->w));
	c->y = CLAMP(c->y, y, MAX(y, y + wh - c->h));

	swa.override_redirect = True;
	swa.background_pixel = 0;
	swa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
		PointerMotionMask | LeaveWindowMask | ExposureMask;
	c->frame = XCreateWindow(dpy, root, c->x, c->y, c->w, c->h, 0, depth, InputOutput, vis,
		CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
	swa.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
	for (int i = 0; i < 4; i++) {
		static const int hc[4] = {CUR_T, CUR_B, CUR_L, CUR_R};
		swa.cursor = cur[hc[i]];
		c->hdl[i] = XCreateWindow(dpy, root, 0, 0, 1, 1, 0, 0, InputOnly, CopyFromParent,
			CWOverrideRedirect | CWEventMask | CWCursor, &swa);
	}
	XAddToSaveSet(dpy, w);
	XSetWindowBorderWidth(dpy, w, 0);
	if (wa->map_state == IsViewable) c->ignore_unmap++;
	XReparentWindow(dpy, w, c->frame, bw, tt);
	XSelectInput(dpy, w, PropertyChangeMask);
	grab_buttons(c, 0);

	for (pp = &clients; *pp; pp = &(*pp)->next);
	*pp = c;
	if (nz < (int)LEN(zorder)) zorder[nz++] = c;
	long desk = curws;
	XChangeProperty(dpy, w, A[NetWMDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&desk, 1);
	set_wm_state(c, NormalState);
	apply(c);
	XMapWindow(dpy, w);
	XMapWindow(dpy, c->frame);
	if (has_state(w, A[NetWMFullscreen])) set_full(c, 1);
	else if (has_state(w, A[NetWMMaxV]) && has_state(w, A[NetWMMaxH])) set_max(c, 1);
	arrange();
	restack();
	focus(c);
}

static void unmanage(Client *c, int destroyed)
{
	Client **pp;
	for (pp = &clients; *pp && *pp != c; pp = &(*pp)->next);
	if (*pp) *pp = c->next;
	z_remove(c);
	for (int i = 0; i < sd_n; i++) if (sd_list[i] == c) sd_list[i] = sd_list[--sd_n];
	if (drag.c == c) { drag.on = 0; drag.c = NULL; XUngrabPointer(dpy, CurrentTime); if (snapwin) XUnmapWindow(dpy, snapwin); }
	if (at_on) for (int i = 0; i < at_n; i++) if (at_list[i] == c) { memmove(&at_list[i], &at_list[i + 1], sizeof *at_list * (size_t)(at_n - i - 1)); at_n--; at_sel = at_n ? at_sel % at_n : 0; }
	if (!destroyed) {
		XGrabServer(dpy);
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		XSelectInput(dpy, c->win, NoEventMask);
		XReparentWindow(dpy, c->win, root, c->x + c_bw(c), c->y + c_th(c));
		XRemoveFromSaveSet(dpy, c->win);
		set_wm_state(c, WithdrawnState);
		XSync(dpy, False);
		XUngrabServer(dpy);
	}
	XDestroyWindow(dpy, c->frame);
	for (int i = 0; i < 4; i++) XDestroyWindow(dpy, c->hdl[i]);
	icon_free(&c->ic);
	if (sel == c) { sel = NULL; free(c); focus_top(); }
	else free(c);
	if (last_click_c == c) last_click_c = NULL;
	arrange();
	ewmh_lists();
	tb_draw_all();
	if (at_on && !at_n) { at_on = 0; XUnmapWindow(dpy, atwin); }
}

/* ---------- taskbar ---------- */

static int pin_has(const char *list, App *a)
{
	char tmp[2048], *t, *sv;
	snprintf(tmp, sizeof tmp, "%s", list);
	for (t = strtok_r(tmp, " ", &sv); t; t = strtok_r(NULL, " ", &sv))
		if (app_resolve(t) == a) return 1;
	return 0;
}

static void pin_toggle(char *list, size_t n, App *a)
{
	char tmp[2048], out[2048] = "", *t, *sv;
	int found = 0;
	snprintf(tmp, sizeof tmp, "%s", list);
	for (t = strtok_r(tmp, " ", &sv); t; t = strtok_r(NULL, " ", &sv)) {
		if (app_resolve(t) == a) { found = 1; continue; }
		if (strlen(out) + strlen(t) + 2 < sizeof out) { if (out[0]) strcat(out, " "); strcat(out, t); }
	}
	if (!found && strlen(out) + strlen(a->id) + 2 < sizeof out) { if (out[0]) strcat(out, " "); strcat(out, a->id); }
	snprintf(list, n, "%s", out);
	cfg_save();
}

static void build_groups(void)
{
	char tmp[1024], *t, *sv;
	int i;
	ng = 0;
	snprintf(tmp, sizeof tmp, "%s", cfg.pinned);
	for (t = strtok_r(tmp, " ", &sv); t && ng < (int)LEN(G); t = strtok_r(NULL, " ", &sv)) {
		App *a = app_resolve(t);
		if (!a) continue;
		for (i = 0; i < ng && G[i].app != a; i++);
		if (i < ng) continue;
		memset(&G[ng], 0, sizeof G[ng]);
		G[ng].app = a;
		G[ng++].pinned = 1;
	}
	for (Client *c = clients; c; c = c->next) {
		if (c->skiptb || c->ws != curws) continue;
		for (i = 0; i < ng; i++)
			if (c->app ? G[i].app == c->app : !G[i].app && !strcasecmp(G[i].key, c->cls)) break;
		if (i == ng) {
			if (ng == (int)LEN(G)) continue;
			memset(&G[ng], 0, sizeof G[ng]);
			G[ng].app = c->app;
			snprintf(G[ng].key, sizeof G[ng].key, "%s", c->cls);
			ng++;
		}
		if (G[i].n < (int)LEN(G[i].w)) G[i].w[G[i].n++] = c;
	}
}

static void clock_str(char *t, size_t tn, char *d, size_t dn)
{
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	strftime(t, tn, RO ? "%H:%M" : "%-I:%M %p", tm);
	strftime(d, dn, RO ? "%d.%m.%Y" : "%-m/%-d/%Y", tm);
}

static int net_glyph(void)
{
	DIR *d = opendir("/sys/class/net");
	struct dirent *e;
	char p[300], st[16];
	int wifi = 0, wifiup = 0, ethup = 0;
	FILE *f;
	struct stat sb;
	if (!d) return G_ETH;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo")) continue;
		snprintf(p, sizeof p, "/sys/class/net/%s/wireless", e->d_name);
		int isw = !stat(p, &sb);
		snprintf(p, sizeof p, "/sys/class/net/%s/operstate", e->d_name);
		st[0] = 0;
		if ((f = fopen(p, "r"))) { if (!fgets(st, sizeof st, f)) st[0] = 0; fclose(f); }
		int up = !strncmp(st, "up", 2);
		snprintf(p, sizeof p, "/sys/class/net/%s/device", e->d_name);
		if (stat(p, &sb)) continue;
		if (isw) { wifi = 1; wifiup |= up; } else ethup |= up;
	}
	closedir(d);
	return wifiup ? G_WIFI : ethup ? G_ETH : wifi ? G_WIFIOFF : G_ETH;
}

static int tray_size(void) { return 20; }

static char kbl[8][16];
static int nkbl, xkb_event = -1;

static int kbd_group(void)
{
	XkbStateRec s;
	return XkbGetState(dpy, XkbUseCoreKbd, &s) == Success ? s.group : 0;
}

static void kbd_refresh(void) { nkbl = kbd_current(kbl, (int)LEN(kbl)); }

/* loads the layouts from the config into the X server; empty config leaves the system default */
static void kbd_apply(void)
{
	char cur[160] = "", a[160];
	if (!cfg.keyboard[0] || strspn(cfg.keyboard, "abcdefghijklmnopqrstuvwxyz0123456789,_-") != strlen(cfg.keyboard)) return;
	kbd_refresh();
	for (int i = 0; i < nkbl; i++) { size_t l = strlen(cur); snprintf(cur + l, sizeof cur - l, "%s%s", i ? "," : "", kbl[i]); }
	if (!strcmp(cur, cfg.keyboard)) return;
	snprintf(a, sizeof a, "setxkbmap -layout %s", cfg.keyboard);
	spawn(a);
}

static void kbd_label(int i, const char **top, const char **bot)
{
	static char up[16];
	const Kbd *k = i < nkbl ? kbd_info(kbl[i]) : NULL;
	if (k) { *top = k->top; *bot = k->bot; return; }
	snprintf(up, sizeof up, "%s", i < nkbl ? kbl[i] : "?");
	for (char *p = up; *p; p++) *p = (char)toupper((unsigned char)*p);
	*top = up; *bot = "";
}

static void tb_layout(int m)
{
	int mw = mons[m].w, i, tot, x, rx;
	char t[32], d[32];
	build_groups();
	ntbi = 0;
	tot = 40 + 4 + 180 + 4 + 44 + ng * 44;
	x = cfg.align_left ? 12 : (mw - tot) / 2;
	tbi[ntbi++] = (TbItem){TB_START, x, 40, 0};
	x += 44;
	tbi[ntbi++] = (TbItem){TB_SEARCH, x, 180, 0};
	x += 184;
	tbi[ntbi++] = (TbItem){TB_TASKVIEW, x, 40, 0};
	x += 44;
	for (i = 0; i < ng && ntbi < (int)LEN(tbi) - 6; i++, x += 44) tbi[ntbi++] = (TbItem){TB_GROUP, x, 40, i};
	rx = mw - 12;
	tbi[ntbi++] = (TbItem){TB_DESK, rx, 12, 0};
	clock_str(t, sizeof t, d, sizeof d);
	int cw = MAX(tw(F_sm, t), tw(F_sm, d)) + 20;
	rx -= cw + 2;
	tbi[ntbi++] = (TbItem){TB_CLOCK, rx, cw, 0};
	int sw = (battery(NULL) >= 0 ? 3 : 2) * 24 + 12;
	rx -= sw + 2;
	tbi[ntbi++] = (TbItem){TB_SYS, rx, sw, 0};
	if (nkbl > 1) {
		rx -= 44 + 2;
		tbi[ntbi++] = (TbItem){TB_LANG, rx, 44, 0};
	}
	if (m == 0 && ntray) {
		int trw = ntray * (tray_size() + 8) + 4;
		rx -= trw;
		tbi[ntbi++] = (TbItem){TB_TRAY, rx, trw, 0};
	}
}

static int group_active(Group *g)
{
	for (int i = 0; i < g->n; i++) if (g->w[i] == sel) return 1;
	return 0;
}

static void tb_draw(int m)
{
	int mw = mons[m].w, i, hover;
	char t[32], d[32];
	if (!mons[m].bar) return;
	tb_layout(m);
	buf_ensure(mw, TBH);
	fill(0, 0, mw, TBH, th.bar);
	fill(0, cfg.bar_top ? TBH - 1 : 0, mw, 1, th.line);
	int iy = cfg.bar_top ? 2 : 43;
	for (i = 0; i < ntbi; i++) {
		TbItem *it = &tbi[i];
		hover = tb_hmon == m && tb_hover == i;
		if (it->kind == TB_GROUP && group_active(&G[it->g])) rrect(it->x, 4, it->w, 40, 5, th.hover, 255);
		if (hover && it->kind != TB_SEARCH && it->kind != TB_TRAY) rrect(it->x, 4, it->w, 40, 5, it->kind == TB_GROUP && group_active(&G[it->g]) ? th.press : th.hover, 255);
		switch (it->kind) {
		case TB_START:
			glyph(G_START, it->x + 8, 12, 24, th.accent);
			if (sm_on) rrect(it->x + 12, iy, 16, 3, 1, th.accent, 255);
			break;
		case TB_SEARCH:
			rbox(it->x, 8, it->w, 32, 16, hover ? th.hover : th.card, th.border);
			glyph(G_SEARCH, it->x + 12, 14, 20, th.sub);
			text(F_ui, it->x + 40, (TBH - fh(F_ui)) / 2, T("Search", "Caută"), th.sub);
			break;
		case TB_GROUP: {
			Group *g = &G[it->g];
			if (g->app && app_icon(g->app)) icon_draw(&g->app->ic, it->x + 8, 12, 24);
			else if (g->n && g->w[0]->hasicon) icon_draw(&g->w[0]->ic, it->x + 8, 12, 24);
			else letter_tile(g->app ? g->app->name : g->key, it->x + 8, 12, 24);
			if (g->n) {
				if (group_active(g)) rrect(it->x + 12, iy, 16, 3, 1, th.accent, 255);
				else rrect(it->x + 17, iy, 6, 3, 1, cfg.dark ? 0x9A9A9A : 0x8A8A8A, 255);
			}
			break;
		}
		case TB_SYS: {
			int x = it->x + 6, chg, b = battery(&chg);
			if (qs_on) rrect(it->x, 4, it->w, 40, 5, th.hover, 255);
			glyph(net_glyph(), x, 14, 20, th.text); x += 24;
			glyph(G_VOL, x, 14, 20, th.text); x += 24;
			if (b >= 0) {
				glyph(G_BATT, x, 14, 20, th.text);
				int fw = (int)(12.0 * CLAMP(b, 0, 100) / 100 + .5);
				rrect(x + 4, 22, MAX(fw, 1), 4, 1, b <= 15 && !chg ? 0xC42B1C : th.text, 255);
			}
			break;
		}
		case TB_LANG: {
			const char *a, *b;
			kbd_label(kbd_group(), &a, &b);
			if (*b) { textc(F_sm, it->x, 7, it->w, a, th.text); textc(F_sm, it->x, 25, it->w, b, th.text); }
			else textc(F_sm, it->x, (TBH - fh(F_sm)) / 2, it->w, a, th.text);
			break;
		}
		case TB_TASKVIEW:
			if (tv_on) rrect(it->x, 4, it->w, 40, 5, th.hover, 255);
			glyph(G_TASKVIEW, it->x + 8, 12, 24, th.text);
			break;
		case TB_CLOCK:
			clock_str(t, sizeof t, d, sizeof d);
			text(F_sm, it->x + it->w - 10 - tw(F_sm, t), 7, t, th.text);
			text(F_sm, it->x + it->w - 10 - tw(F_sm, d), 25, d, th.text);
			break;
		case TB_DESK:
			fill(it->x, 14, 1, 20, th.line);
			break;
		case TB_TRAY:
			tray_x = it->x + 4;
			break;
		}
	}
	XCopyArea(dpy, B.pm, mons[m].bar, DefaultGC(dpy, scr), 0, 0, mw, TBH, 0, 0);
	if (m == 0) {
		int sz = tray_size(), x = tray_x;
		for (i = 0; i < ntray; i++, x += sz + 8) {
			XSetWindowBackground(dpy, trayicons[i], th.bar);
			XMoveResizeWindow(dpy, trayicons[i], x + 4, (TBH - sz) / 2, sz, sz);
		}
	}
}

static void tb_draw_all(void)
{
	for (int i = 0; i < nmons; i++) tb_draw(i);
}

static int tb_hit(int m, int x)
{
	tb_layout(m);
	for (int i = 0; i < ntbi; i++)
		if (x >= tbi[i].x && x < tbi[i].x + tbi[i].w) return i;
	return -1;
}

/* ---------- tray ---------- */

static void tray_setup(void)
{
	char name[64];
	XEvent ev = {0};
	unsigned long orient = 0;
	snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", scr);
	A_tray = XInternAtom(dpy, name, False);
	trayselwin = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, trayselwin, A[TrayOrient], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&orient, 1);
	XSetSelectionOwner(dpy, A_tray, trayselwin, CurrentTime);
	if (XGetSelectionOwner(dpy, A_tray) != trayselwin) return;
	ev.xclient.type = ClientMessage;
	ev.xclient.window = root;
	ev.xclient.message_type = A[Manager];
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = (long)A_tray;
	ev.xclient.data.l[2] = (long)trayselwin;
	XSendEvent(dpy, root, False, StructureNotifyMask, &ev);
}

static int tray_index(Window w)
{
	for (int i = 0; i < ntray; i++) if (trayicons[i] == w) return i;
	return -1;
}

static void tray_dock(Window w)
{
	XEvent ev = {0};
	if (!w || tray_index(w) >= 0 || ntray >= (int)LEN(trayicons) || !mons[0].bar) return;
	trayicons[ntray++] = w;
	XSelectInput(dpy, w, StructureNotifyMask);
	XReparentWindow(dpy, w, mons[0].bar, 0, 0);
	XSetWindowBackground(dpy, w, th.bar);
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = A[Xembed];
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = 0;
	ev.xclient.data.l[3] = (long)mons[0].bar;
	XSendEvent(dpy, w, False, NoEventMask, &ev);
	XMapWindow(dpy, w);
	tb_draw_all();
}

static void tray_remove(Window w)
{
	int i = tray_index(w);
	if (i < 0) return;
	memmove(&trayicons[i], &trayicons[i + 1], sizeof *trayicons * (size_t)(ntray - i - 1));
	ntray--;
	tb_draw_all();
}

static void tray_release(void)
{
	for (int i = 0; i < ntray; i++) {
		XSelectInput(dpy, trayicons[i], NoEventMask);
		XUnmapWindow(dpy, trayicons[i]);
		XReparentWindow(dpy, trayicons[i], root, 0, 0);
	}
	ntray = 0;
}

/* ---------- popups ---------- */

static Window popup_win(void)
{
	XSetWindowAttributes swa;
	swa.override_redirect = True;
	swa.background_pixel = th.menu;
	swa.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask | ExposureMask;
	return XCreateWindow(dpy, root, 0, 0, 10, 10, 0, depth, InputOutput, vis,
		CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
}

static void grab_input(void)
{
	if (grabbed) return;
	XGrabPointer(dpy, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, cur[CUR_NORMAL], CurrentTime);
	for (int i = 0; i < 20 && XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess; i++)
		usleep(5000);
	grabbed = 1;
}

static void ungrab_input(void)
{
	if (!grabbed || sm_on || ctx_on || qs_on || at_on || tv_on) return;
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	grabbed = 0;
}

static void lock_screen(void)
{
	if (have("glue-action")) spawn("glue-action lock");
	else if (have("i3lock")) spawn("i3lock -c 202020");
	else if (have("xsecurelock")) spawn("xsecurelock");
	else spawn("loginctl lock-session");
}

static void task_manager(void)
{
	char cmd[300];
	if (have("missioncenter")) spawn("missioncenter");
	else if (have("gnome-system-monitor")) spawn("gnome-system-monitor");
	else { snprintf(cmd, sizeof cmd, "%s -e %s", cfg.terminal, have("htop") ? "htop" : "top"); spawn(cmd); }
}

/* --- context menu --- */

static void ctx_close(void)
{
	if (!ctx_on) return;
	ctx_on = 0;
	XUnmapWindow(dpy, ctxwin);
	ungrab_input();
}

static void ctx_add(const char *label, int id)
{
	if (nmi >= (int)LEN(mi)) return;
	snprintf(mi[nmi].label, sizeof mi[nmi].label, "%s", label);
	mi[nmi++].id = id;
}

static int ctx_row_h(int i) { return mi[i].id < 0 ? 9 : 34; }

static void ctx_draw(void)
{
	int y = 4;
	buf_ensure(ctx_w, ctx_h);
	fill(0, 0, ctx_w, ctx_h, th.border);
	fill(1, 1, ctx_w - 2, ctx_h - 2, th.menu);
	for (int i = 0; i < nmi; i++) {
		int h = ctx_row_h(i);
		if (mi[i].id < 0) fill(8, y + 4, ctx_w - 16, 1, th.border);
		else {
			if (ctx_hover == i) rrect(4, y, ctx_w - 8, h, 4, th.hover, 255);
			textfit(F_ui, 16, y + (h - fh(F_ui)) / 2, ctx_w - 32, mi[i].label, th.text);
		}
		y += h;
	}
	blit(ctxwin, ctx_w, ctx_h);
}

static void ctx_show(int x, int y, int bottom_anchor)
{
	int m = mon_at(x, y), i;
	if (!nmi) return;
	ctx_w = 200;
	ctx_h = 8;
	for (i = 0; i < nmi; i++) { ctx_w = MAX(ctx_w, tw(F_ui, mi[i].label) + 40); ctx_h += ctx_row_h(i); }
	ctx_w = MIN(ctx_w, 420);
	if (bottom_anchor) y -= ctx_h;
	x = CLAMP(x, mons[m].x + 4, mons[m].x + mons[m].w - ctx_w - 4);
	y = CLAMP(y, mons[m].y + 4, mons[m].y + mons[m].h - ctx_h - 4);
	ctx_x = x; ctx_y = y;
	if (!ctxwin) ctxwin = popup_win();
	XMoveResizeWindow(dpy, ctxwin, x, y, ctx_w, ctx_h);
	shape_round(ctxwin, ctx_w, ctx_h, 8);
	ctx_hover = -1;
	ctx_on = 1;
	XMapRaised(dpy, ctxwin);
	ctx_draw();
	grab_input();
}

static int ctx_hit(int y)
{
	int yy = 4;
	for (int i = 0; i < nmi; i++) {
		int h = ctx_row_h(i);
		if (y >= yy && y < yy + h) return mi[i].id < 0 ? -1 : i;
		yy += h;
	}
	return -1;
}

static void sm_close(void);
static void sm_open(int mode, int m);
static void show_desktop(void);

static void power_menu_items(void)
{
	nmi = 0;
	ctx_add(T("Lock", "Blochează"), 1);
	ctx_add(T("Sign out", "Deconectare"), 2);
	ctx_add("", -1);
	ctx_add(T("Sleep", "Repaus"), 3);
	ctx_add(T("Restart", "Repornire"), 4);
	ctx_add(T("Shut down", "Închidere"), 5);
}

static void ctx_activate(int i)
{
	int id = mi[i].id;
	int kind = ctx_kind;
	App *a = ctx_app;
	Client *c;
	ctx_close();
	switch (kind) {
	case CX_POWER:
	case CX_CAD:
		sm_close();
		if (id == 1) lock_screen();
		else if (id == 2) running = 0;
		else if (id == 3) spawn("loginctl suspend");
		else if (id == 4) spawn("loginctl reboot");
		else if (id == 5) spawn("loginctl poweroff");
		else if (id == 6) task_manager();
		break;
	case CX_GROUP:
		if (id >= 100 && id < 100 + (int)LEN(ctx_wins)) activate(bywin(ctx_wins[id - 100]));
		else if (id == 1 && a) app_launch(a);
		else if (id == 2 && a) { pin_toggle(cfg.pinned, sizeof cfg.pinned, a); tb_draw_all(); }
		else if (id == 3) for (int k = 0; k < (int)LEN(ctx_wins) && ctx_wins[k]; k++) if ((c = bywin(ctx_wins[k]))) close_client(c);
		break;
	case CX_STARTAPP:
		if (id == 1 && a) { sm_close(); app_launch(a); }
		else if (id == 2 && a) {
			if (!cfg.start[0]) {
				/* materialize the default start pins before editing them */
				char *s = cfg.start;
				for (int k = 0; k < 4 && k < sm_nres; k++);
				extern int sm_default_pins(App **out, int max);
				App *list[48];
				int n = sm_default_pins(list, 48);
				for (int k = 0; k < n; k++) {
					size_t l = strlen(s);
					snprintf(s + l, sizeof cfg.start - l, "%s%s", l ? " " : "", list[k]->id);
				}
			}
			pin_toggle(cfg.start, sizeof cfg.start, a);
			if (sm_on) sm_draw();
		} else if (id == 3 && a) { pin_toggle(cfg.pinned, sizeof cfg.pinned, a); tb_draw_all(); }
		break;
	case CX_DESK:
		if (id == 1) spawn("gluewin-settings --page personalization");
		else if (id == 2) spawn("gluewin-settings --page display");
		else if (id == 3) spawn(cfg.terminal);
		break;
	case CX_LANG:
		if (id >= 10) XkbLockGroup(dpy, XkbUseCoreKbd, (unsigned)(id - 10));
		else if (id == 1) spawn("gluewin-settings --page language");
		break;
	case CX_TASKBAR:
		if (id == 1) task_manager();
		else if (id == 2) spawn("gluewin-settings --page personalization");
		break;
	case CX_WIN:
		c = bywin(ctx_win1);
		if (!c) break;
		if (id == 1) set_max(c, 0);
		else if (id == 2) minimize(c);
		else if (id == 3) set_max(c, 1);
		else if (id == 4) close_client(c);
		break;
	}
}

/* --- start menu --- */

static int sm_footer(void) { return 64; }

int sm_default_pins(App **out, int max)
{
	char tmp[2048], *t, *sv;
	int n = 0, i;
	App *a;
	snprintf(tmp, sizeof tmp, "%s", cfg.start[0] ? cfg.start : cfg.pinned);
	for (t = strtok_r(tmp, " ", &sv); t && n < max; t = strtok_r(NULL, " ", &sv))
		if ((a = app_resolve(t))) {
			for (i = 0; i < n && out[i] != a; i++);
			if (i == n) out[n++] = a;
		}
	if (cfg.start[0]) return n;
	for (int k = 0; k < napps && n < 18; k++) {
		for (i = 0; i < n && out[i] != &apps[k]; i++);
		if (i == n) out[n++] = &apps[k];
	}
	return n;
}

static int score(App *a, const char *q)
{
	char *p = strcasestr(a->name, q);
	if (p == a->name) return 0;
	if (p && (p[-1] == ' ' || p[-1] == '-')) return 1;
	if (p) return 2;
	if (strcasestr(a->search, q) || strcasestr(a->exec, q)) return 3;
	return -1;
}

static void sm_search(void)
{
	int k, i;
	sm_nres = 0;
	for (k = 0; k < 4; k++)
		for (i = 0; i < napps && sm_nres < (int)LEN(sm_res); i++)
			if (score(&apps[i], sm_q) == k) sm_res[sm_nres++] = i;
	sm_sel = 0;
	sm_scroll = 0;
}

static void hit_add(int x, int y, int w, int h, int kind, int idx)
{
	if (nsmh < (int)LEN(smh)) smh[nsmh++] = (Hit){x, y, w, h, kind, idx};
}

static void hover_bg(int i, int x, int y, int w, int h)
{
	if (sm_hover == i) rrect(x, y, w, h, 6, th.hover, 255);
}

static void sm_draw(void)
{
	int W = SMW, H = SMH, foot = sm_footer(), top = 124, i, x, y;
	char name[128];
	nsmh = 0;
	buf_ensure(W, H);
	fill(0, 0, W, H, th.border);
	fill(1, 1, W - 2, H - 2, th.menu);

	rbox(28, 24, W - 56, 38, 19, th.card, th.border);
	fill(28 + 19, 24 + 36, W - 56 - 38, 2, th.accent);
	glyph(G_SEARCH, 44, 33, 20, th.sub);
	if (sm_q[0]) {
		textfit(F_ui, 74, 24 + (38 - fh(F_ui)) / 2, W - 140, sm_q, th.text);
		int cx = 74 + MIN(tw(F_ui, sm_q), W - 140);
		fill(cx + 1, 33, 1, 20, th.text);
	} else {
		fill(74, 33, 1, 20, th.text);
		text(F_ui, 78, 24 + (38 - fh(F_ui)) / 2, sm_mode == 3 ? T("Type an app name or a command", "Scrie numele unei aplicații sau o comandă")
			: T("Search for apps, settings and documents", "Caută aplicații, setări și documente"), th.sub);
	}
	hit_add(28, 24, W - 56, 38, H_SEARCH, 0);

	int listbot = H - foot - 8;
	if (sm_q[0]) {
		text(F_md, 48, 84, sm_nres ? T("Best match", "Cea mai bună potrivire") : T("No apps found", "Nicio aplicație găsită"), th.text);
		y = top - 8;
		int rows = (listbot - y) / 48;
		if (!sm_nres) {
			snprintf(name, sizeof name, T("Run “%s”", "Rulează „%s”"), sm_q);
			int idx = nsmh;
			hover_bg(idx, 32, y, W - 64, 48);
			if (sm_sel == 0) rrect(32, y, W - 64, 48, 6, th.hover, 255);
			glyph(G_CHEVR, 48, y + 12, 24, th.text);
			textfit(F_ui, 88, y + (48 - fh(F_ui)) / 2, W - 140, name, th.text);
			hit_add(32, y, W - 64, 48, H_RUN, 0);
		}
		sm_sel = CLAMP(sm_sel, 0, MAX(sm_nres - 1, 0));
		if (sm_sel < sm_scroll) sm_scroll = sm_sel;
		if (sm_sel >= sm_scroll + rows) sm_scroll = sm_sel - rows + 1;
		for (i = sm_scroll; i < sm_nres && i < sm_scroll + rows; i++, y += 48) {
			App *a = &apps[sm_res[i]];
			int idx = nsmh;
			if (i == sm_sel) rrect(32, y, W - 64, 48, 6, th.hover, 255);
			else hover_bg(idx, 32, y, W - 64, 48);
			if (i == sm_sel) rrect(32, y + 14, 3, 20, 1, th.accent, 255);
			if (app_icon(a)) icon_draw(&a->ic, 48, y + 10, 28);
			else letter_tile(a->name, 48, y + 10, 28);
			textfit(F_ui, 92, y + 6, W - 150, a->name, th.text);
			text(F_sm, 92, y + 26, T("App", "Aplicație"), th.sub);
			hit_add(32, y, W - 64, 48, H_APP, sm_res[i]);
		}
	} else if (sm_mode == 1) {
		text(F_md, 48, 84, T("All apps", "Toate aplicațiile"), th.text);
		const char *bl = T("Back", "Înapoi");
		int bw = tw(F_sm, bl) + 40, bx = W - 48 - bw, idx = nsmh;
		rbox(bx, 78, bw, 28, 4, sm_hover == idx ? th.hover : th.card, th.border);
		glyph(G_CHEVL, bx + 8, 84, 16, th.text);
		text(F_sm, bx + 28, 78 + (28 - fh(F_sm)) / 2, bl, th.text);
		hit_add(bx, 78, bw, 28, H_BACK, 0);
		/* rows: letter headers + apps */
		int rows = (listbot - top) / 40, r = 0;
		char last = 0;
		int maxr = 0;
		for (i = 0; i < napps; i++) {
			char l = (char)toupper((unsigned char)apps[i].name[0]);
			if (!isalpha((unsigned char)l)) l = '#';
			if (l != last) { maxr++; last = l; }
			maxr++;
		}
		sm_scroll = CLAMP(sm_scroll, 0, MAX(maxr - rows, 0));
		last = 0;
		y = top;
		for (i = 0; i < napps; i++) {
			char l = (char)toupper((unsigned char)apps[i].name[0]);
			if (!isalpha((unsigned char)l)) l = '#';
			if (l != last) {
				last = l;
				if (r >= sm_scroll && r < sm_scroll + rows) {
					char hb[2] = {l, 0};
					text(F_md, 56, y + (40 - fh(F_md)) / 2, hb, th.text);
					y += 40;
				}
				r++;
			}
			if (r >= sm_scroll && r < sm_scroll + rows) {
				int idx2 = nsmh;
				hover_bg(idx2, 40, y, W - 80, 40);
				if (app_icon(&apps[i])) icon_draw(&apps[i].ic, 56, y + 8, 24);
				else letter_tile(apps[i].name, 56, y + 8, 24);
				textfit(F_ui, 96, y + (40 - fh(F_ui)) / 2, W - 160, apps[i].name, th.text);
				hit_add(40, y, W - 80, 40, H_APP, i);
				y += 40;
			}
			r++;
		}
		if (maxr > rows) {
			int th_ = (listbot - top) * rows / maxr, ty = top + (listbot - top - th_) * sm_scroll / MAX(maxr - rows, 1);
			rrect(W - 20, ty, 4, th_, 2, th.sub, 160);
		}
	} else {
		App *pins[48];
		int n = sm_default_pins(pins, 48), cols = 6, cw = (W - 64) / cols, ch = 90;
		int rows = (listbot - top) / ch;
		text(F_md, 48, 84, T("Pinned", "Fixate"), th.text);
		const char *al = T("All apps", "Toate aplicațiile");
		int bw = tw(F_sm, al) + 40, bx = W - 48 - bw, idx = nsmh;
		rbox(bx, 78, bw, 28, 4, sm_hover == idx ? th.hover : th.card, th.border);
		text(F_sm, bx + 12, 78 + (28 - fh(F_sm)) / 2, al, th.text);
		glyph(G_CHEVR, bx + bw - 24, 84, 16, th.text);
		hit_add(bx, 78, bw, 28, H_ALL, 0);
		for (i = 0; i < n && i < cols * rows; i++) {
			x = 32 + (i % cols) * cw;
			y = top + (i / cols) * ch;
			int idx2 = nsmh;
			hover_bg(idx2, x + 2, y, cw - 4, ch - 4);
			if (app_icon(pins[i])) icon_draw(&pins[i]->ic, x + (cw - 32) / 2, y + 14, 32);
			else letter_tile(pins[i]->name, x + (cw - 32) / 2, y + 14, 32);
			textc(F_sm, x + 4, y + 56, cw - 8, pins[i]->name, th.text);
			hit_add(x + 2, y, cw - 4, ch - 4, H_APP, (int)(pins[i] - apps));
		}
	}

	y = H - foot;
	fill(1, y, W - 2, foot - 1, th.foot);
	fill(1, y, W - 2, 1, th.border);
	user_name(name, sizeof name);
	rrect(48, y + 16, 32, 32, 16, th.accent, 255);
	{
		char l[8];
		int k = 1;
		while (name[k] && (name[k] & 0xC0) == 0x80) k++;
		snprintf(l, sizeof l, "%.*s", k, name);
		if (k == 1) l[0] = (char)toupper((unsigned char)l[0]);
		textc(F_md, 48, y + 16 + (32 - fh(F_md)) / 2, 32, l, th.onaccent);
	}
	textfit(F_ui, 92, y + (foot - fh(F_ui)) / 2, W / 2 - 92, name, th.text);
	static const int kinds[3] = {H_FILES, H_SETTINGS, H_POWER}, gl[3] = {G_FOLDER, G_GEAR, G_POWER};
	for (i = 0; i < 3; i++) {
		x = W - 48 - 40 * (3 - i) - 8 * (2 - i);
		int idx = nsmh;
		hover_bg(idx, x, y + 12, 40, 40);
		glyph(gl[i], x + 10, y + 22, 20, th.text);
		hit_add(x, y + 12, 40, 40, kinds[i], 0);
	}
	blit(smwin, W, H);
}

static void sm_open(int mode, int m)
{
	int mh;
	close_popups();
	sm_mon = m;
	mh = mons[m].h - TBH - 24;
	SMH = MIN(700, mh);
	SMW = MIN(640, mons[m].w - 24);
	sm_x = cfg.align_left ? mons[m].x + 12 : mons[m].x + (mons[m].w - SMW) / 2;
	sm_y = pop_y(m, SMH);
	sm_mode = mode;
	sm_q[0] = 0;
	sm_sel = sm_scroll = 0;
	sm_hover = -1;
	if (!smwin) smwin = popup_win();
	XMoveResizeWindow(dpy, smwin, sm_x, sm_y, SMW, SMH);
	shape_round(smwin, SMW, SMH, 8);
	sm_on = 1;
	XMapRaised(dpy, smwin);
	sm_draw();
	grab_input();
	tb_draw_all();
}

static void sm_close(void)
{
	if (!sm_on) return;
	sm_on = 0;
	XUnmapWindow(dpy, smwin);
	ctx_close();
	ungrab_input();
	tb_draw_all();
}

static void sm_hit(Hit *h, int button, int rx, int ry)
{
	App *a = h->kind == H_APP ? &apps[h->idx] : NULL;
	if (button == 3 && a) {
		nmi = 0;
		ctx_kind = CX_STARTAPP;
		ctx_app = a;
		ctx_add(T("Open", "Deschide"), 1);
		ctx_add(pin_has(cfg.start[0] ? cfg.start : "", a) ? T("Unpin from Start", "Anulează fixarea din Start")
			: T("Pin to Start", "Fixează la Start"), 2);
		ctx_add(pin_has(cfg.pinned, a) ? T("Unpin from taskbar", "Anulează fixarea din bara de activități")
			: T("Pin to taskbar", "Fixează în bara de activități"), 3);
		ctx_show(rx, ry, 0);
		return;
	}
	if (button != 1) return;
	switch (h->kind) {
	case H_APP: sm_close(); app_launch(a); break;
	case H_ALL: sm_mode = 1; sm_scroll = 0; sm_hover = -1; sm_draw(); break;
	case H_BACK: sm_mode = 0; sm_hover = -1; sm_draw(); break;
	case H_FILES: sm_close(); spawn(cfg.files); break;
	case H_SETTINGS: sm_close(); spawn("gluewin-settings"); break;
	case H_RUN: sm_close(); spawn(sm_q); break;
	case H_POWER:
		power_menu_items();
		ctx_kind = CX_POWER;
		ctx_show(rx, ry - 8, 1);
		break;
	}
}

static void sm_key(XKeyEvent *e)
{
	char buf[32];
	KeySym ks;
	int n = XLookupString(e, buf, sizeof buf - 1, &ks, NULL);
	buf[MAX(n, 0)] = 0;
	switch (ks) {
	case XK_Escape:
		if (sm_q[0]) { sm_q[0] = 0; sm_mode = sm_mode == 3 ? 0 : sm_mode; sm_draw(); }
		else sm_close();
		return;
	case XK_BackSpace: {
		size_t l = strlen(sm_q);
		while (l > 0 && (sm_q[--l] & 0xC0) == 0x80);
		sm_q[l] = 0;
		sm_search();
		sm_draw();
		return;
	}
	case XK_Return:
	case XK_KP_Enter:
		if (!sm_q[0]) return;
		sm_close();
		if (sm_nres) app_launch(&apps[sm_res[sm_sel]]);
		else spawn(sm_q);
		return;
	case XK_Down: sm_sel++; if (sm_mode == 1 && !sm_q[0]) sm_scroll += 3; sm_draw(); return;
	case XK_Up: sm_sel = MAX(sm_sel - 1, 0); if (sm_mode == 1 && !sm_q[0]) sm_scroll = MAX(sm_scroll - 3, 0); sm_draw(); return;
	}
	if (n > 0 && (unsigned char)buf[0] >= 32 && buf[0] != 127 && !(e->state & (ControlMask | Mod4Mask | Mod1Mask))) {
		/* XLookupString gives Latin-1: widen it to UTF-8 */
		char u[8];
		unsigned char ch = (unsigned char)buf[0];
		if (ch < 0x80) { u[0] = (char)ch; u[1] = 0; }
		else { u[0] = (char)(0xC0 | (ch >> 6)); u[1] = (char)(0x80 | (ch & 0x3F)); u[2] = 0; }
		if (strlen(sm_q) + strlen(u) < sizeof sm_q) strcat(sm_q, u);
		sm_search();
		sm_draw();
	}
}

/* --- quick settings --- */

static int qs_layout_bri(void) { return qs_bri >= 0; }

static void qs_draw(void)
{
	int W = QSW, H = qs_h, y = 16, i, tw_ = (W - 32 - 16) / 3, chg, b;
	static const int tg[3] = {G_WIFI, G_BT, G_MOON};
	const char *tl[3] = {"Wi-Fi", "Bluetooth", T("Dark mode", "Mod întunecat")};
	int ton[3] = {qs_wifi, qs_bt, cfg.dark};
	buf_ensure(W, H);
	fill(0, 0, W, H, th.border);
	fill(1, 1, W - 2, H - 2, th.menu);
	for (i = 0; i < 3; i++) {
		int x = 16 + i * (tw_ + 8);
		unsigned bg = ton[i] ? th.accent : (qs_hover == i ? th.hover : th.card), fg = ton[i] ? th.onaccent : th.text;
		if (ton[i]) rrect(x, y, tw_, 60, 6, bg, 255);
		else rbox(x, y, tw_, 60, 6, bg, th.border);
		glyph(tg[i], x + 10, y + 8, 20, fg);
		textfit(F_sm, x + 10, y + 36, tw_ - 16, tl[i], fg);
	}
	y += 60 + 20;
	for (i = 0; i < 2; i++) {
		int v = i == 0 ? qs_bri : qs_vol, maxv = i == 0 ? qs_brimax : 100;
		if (i == 0 && !qs_layout_bri()) continue;
		if (v < 0) v = 0;
		int pct = maxv > 0 ? v * 100 / maxv : 0, tx = 52, tw2 = W - 52 - 56;
		glyph(i == 0 ? G_SUN : (qs_muted || pct == 0 ? G_MUTE : G_VOL), 16, y + 2, 20, th.text);
		rrect(tx, y + 10, tw2, 4, 2, cfg.dark ? 0x9A9A9A : 0x8A8A8A, 255);
		rrect(tx, y + 10, MAX(tw2 * pct / 100, 4), 4, 2, th.accent, 255);
		int kx = tx + tw2 * pct / 100;
		rrect(kx - 10, y + 2, 20, 20, 10, th.card, 255);
		rrect(kx - 10, y + 2, 20, 20, 10, th.border, 120);
		rrect(kx - 9, y + 3, 18, 18, 9, th.card, 255);
		rrect(kx - 6, y + 6, 12, 12, 6, th.accent, 255);
		char s[8];
		snprintf(s, sizeof s, "%d", pct);
		text(F_sm, W - 16 - tw(F_sm, s), y + (24 - fh(F_sm)) / 2, s, th.text);
		y += 44;
	}
	y = H - 52;
	fill(1, y, W - 2, 51, th.foot);
	fill(1, y, W - 2, 1, th.border);
	b = battery(&chg);
	if (b >= 0) {
		char s[32];
		glyph(G_BATT, 16, y + 16, 20, th.text);
		rrect(20, y + 24, MAX(12 * b / 100, 1), 4, 1, th.text, 255);
		snprintf(s, sizeof s, "%d%%%s", b, chg ? T(" · charging", " · se încarcă") : "");
		text(F_sm, 44, y + (52 - fh(F_sm)) / 2, s, th.text);
	}
	if (qs_hover == QS_GEAR) rrect(W - 52, y + 6, 40, 40, 6, th.hover, 255);
	glyph(G_GEAR, W - 42, y + 16, 20, th.text);
	blit(qswin, W, H);
}

static int qs_hit(int x, int y, int *val)
{
	int W = QSW, tw_ = (W - 32 - 16) / 3, yy = 16 + 80;
	if (y >= 16 && y < 76)
		for (int i = 0; i < 3; i++) if (x >= 16 + i * (tw_ + 8) && x < 16 + i * (tw_ + 8) + tw_) return i;
	for (int i = 0; i < 2; i++) {
		if (i == 0 && !qs_layout_bri()) continue;
		if (y >= yy - 6 && y < yy + 30) {
			*val = CLAMP((x - 52) * 100 / (W - 52 - 56), 0, 100);
			return i == 0 ? QS_BRI : QS_VOL;
		}
		yy += 44;
	}
	if (y >= qs_h - 52 && x >= W - 52) return QS_GEAR;
	return -1;
}

static void qs_apply(int which, int pct, int final)
{
	char cmd[128];
	long long t = now_ms();
	if (which == QS_VOL) qs_vol = pct, qs_muted = 0;
	else qs_bri = qs_brimax * pct / 100;
	qs_draw();
	if (!final && t - qs_last < 70) return;
	qs_last = t;
	if (which == QS_VOL) volume_set(pct);
	else { snprintf(cmd, sizeof cmd, "brightnessctl -q set %d%%", MAX(pct, 1)); spawn(cmd); }
}

static void qs_open(void)
{
	char b[512];
	int m = 0, x;
	close_popups();
	qs_wifi = run_read("nmcli radio wifi 2>/dev/null", b, sizeof b) && !strncmp(b, "enabled", 7);
	qs_bt = run_read("timeout 1 bluetoothctl show 2>/dev/null", b, sizeof b) && strstr(b, "Powered: yes");
	qs_vol = volume_get(&qs_muted);
	qs_bri = backlight(&qs_brimax, qs_bldev, sizeof qs_bldev);
	qs_h = 16 + 60 + 20 + (qs_bri >= 0 ? 44 : 0) + 44 + 8 + 52;
	x = mons[m].x + mons[m].w - QSW - 12;
	if (!qswin) qswin = popup_win();
	XMoveResizeWindow(dpy, qswin, x, pop_y(m, qs_h), QSW, qs_h);
	shape_round(qswin, QSW, qs_h, 8);
	qs_on = 1;
	qs_hover = -1;
	XMapRaised(dpy, qswin);
	qs_draw();
	grab_input();
	tb_draw_all();
}

static void qs_close(void)
{
	if (!qs_on) return;
	qs_on = 0;
	qs_drag = 0;
	XUnmapWindow(dpy, qswin);
	ungrab_input();
	tb_draw_all();
}

static void reload(void);

static void qs_press(int x, int y)
{
	int v = 0, h = qs_hit(x, y, &v);
	switch (h) {
	case QS_WIFI: qs_wifi = !qs_wifi; spawn(qs_wifi ? "nmcli radio wifi on" : "nmcli radio wifi off"); qs_draw(); break;
	case QS_BT: qs_bt = !qs_bt; spawn(qs_bt ? "rfkill unblock bluetooth; bluetoothctl power on" : "bluetoothctl power off"); qs_draw(); break;
	case QS_DARK: cfg.dark = !cfg.dark; cfg_save(); theme_export(); reload(); break;
	case QS_BRI: case QS_VOL: qs_drag = h; qs_apply(h, v, 0); break;
	case QS_GEAR: qs_close(); spawn("gluewin-settings"); break;
	}
}

/* --- alt-tab --- */

static int stamp_cmp(const void *a, const void *b)
{
	unsigned long x = (*(Client *const *)a)->stamp, y = (*(Client *const *)b)->stamp;
	return x < y ? 1 : x > y ? -1 : 0;
}

static void at_draw(void)
{
	int cw = 128, W = at_n * cw + 24, H = 150, i;
	buf_ensure(W, H);
	fill(0, 0, W, H, th.border);
	fill(1, 1, W - 2, H - 2, th.menu);
	for (i = 0; i < at_n; i++) {
		Client *c = at_list[i];
		int x = 12 + i * cw;
		if (i == at_sel) { rrect(x + 2, 10, cw - 4, H - 20, 8, th.accent, 255); rrect(x + 5, 13, cw - 10, H - 26, 6, th.hover, 255); }
		if (c->hasicon) icon_draw(&c->ic, x + (cw - 48) / 2, 30, 48);
		else if (c->app && app_icon(c->app)) icon_draw(&c->app->ic, x + (cw - 48) / 2, 30, 48);
		else letter_tile(c->title[0] ? c->title : c->cls, x + (cw - 48) / 2, 30, 48);
		textc(F_sm, x + 10, 96, cw - 20, c->title[0] ? c->title : c->cls, th.text);
	}
	blit(atwin, W, H);
}

static void at_end(int commit)
{
	if (!at_on) return;
	at_on = 0;
	XUnmapWindow(dpy, atwin);
	ungrab_input();
	if (commit && at_n) activate(at_list[at_sel]);
}

static void at_start(int back)
{
	int m = pointer_mon(), W, H = 150;
	char keys[32];
	close_popups();
	at_n = 0;
	for (Client *c = clients; c && at_n < (int)LEN(at_list); c = c->next)
		if (!c->skiptb && c->ws == curws) at_list[at_n++] = c;
	if (!at_n) return;
	qsort(at_list, (size_t)at_n, sizeof *at_list, stamp_cmp);
	at_n = MIN(at_n, (mons[m].w - 80) / 128);
	if (at_n <= 0) return;
	at_sel = at_n > 1 ? (back ? at_n - 1 : 1) : 0;
	W = at_n * 128 + 24;
	if (!atwin) atwin = popup_win();
	XMoveResizeWindow(dpy, atwin, mons[m].x + (mons[m].w - W) / 2, mons[m].y + (mons[m].h - H) / 2, W, H);
	shape_round(atwin, W, H, 8);
	at_on = 1;
	XMapRaised(dpy, atwin);
	at_draw();
	grab_input();
	/* alt may already be up after a quick tap */
	XQueryKeymap(dpy, keys);
	KeyCode l = XKeysymToKeycode(dpy, XK_Alt_L), r = XKeysymToKeycode(dpy, XK_Alt_R);
	if (!(keys[l / 8] & (1 << (l % 8))) && !(keys[r / 8] & (1 << (r % 8)))) at_end(1);
}

/* --- task view: one card per desktop in use, plus "New desktop" --- */

enum { TVW = 200, TVH = 132 };

static void tv_draw(void)
{
	int i, x;
	char t[64];
	buf_ensure(tv_w, tv_h);
	fill(0, 0, tv_w, tv_h, th.border);
	fill(1, 1, tv_w - 2, tv_h - 2, th.menu);
	for (i = 0; i < tv_n; i++) {
		int ws = tv_ws[i], k = 0;
		x = 16 + i * (TVW + 12);
		if (ws == curws) rrect(x - 3, 13, TVW + 6, TVH + 6, 9, th.accent, 255);
		rbox(x, 16, TVW, TVH, 7, tv_hover == i ? th.hover : th.card, th.border);
		if (ws < 0) {
			glyph(G_PLUS, x + (TVW - 32) / 2, 16 + 34, 32, th.text);
			textc(F_ui, x, 16 + 82, TVW, T("New desktop", "Desktop nou"), th.text);
			continue;
		}
		snprintf(t, sizeof t, "Desktop %d", i + 1);
		text(F_md, x + 14, 26, t, th.text);
		for (Client *c = clients; c; c = c->next) {
			if (c->ws != ws || c->skiptb) continue;
			int ix = x + 14 + (k % 5) * 36, iy = 58 + (k / 5) * 38;
			if (k++ >= 10) break;
			if (c->hasicon) icon_draw(&c->ic, ix, iy, 28);
			else if (c->app && app_icon(c->app)) icon_draw(&c->app->ic, ix, iy, 28);
			else letter_tile(c->title[0] ? c->title : c->cls, ix, iy, 28);
		}
		if (!k) text(F_sm, x + 14, 64, T("No windows", "Nicio fereastră"), th.sub);
	}
	blit(tvwin, tv_w, tv_h);
}

static void tv_close(void)
{
	if (!tv_on) return;
	tv_on = 0;
	XUnmapWindow(dpy, tvwin);
	ungrab_input();
	tb_draw_all();
}

static void tv_open(int m)
{
	int used = 1 << curws, i;
	close_popups();
	for (Client *c = clients; c; c = c->next) if (!c->skiptb) used |= 1 << c->ws;
	tv_n = 0;
	for (i = 0; i < NWS; i++) if ((used >> i) & 1) tv_ws[tv_n++] = i;
	if (tv_n < NWS) tv_ws[tv_n++] = -1;
	tv_w = MIN(tv_n * (TVW + 12) + 20, mons[m].w - 24);
	tv_h = TVH + 32;
	if (!tvwin) tvwin = popup_win();
	XMoveResizeWindow(dpy, tvwin, mons[m].x + (mons[m].w - tv_w) / 2, pop_y(m, tv_h), tv_w, tv_h);
	shape_round(tvwin, tv_w, tv_h, 8);
	tv_on = 1;
	tv_hover = -1;
	XMapRaised(dpy, tvwin);
	tv_draw();
	grab_input();
	tb_draw_all();
}

static int tv_hit(int x, int y)
{
	if (y < 16 || y >= 16 + TVH) return -1;
	for (int i = 0; i < tv_n; i++) if (x >= 16 + i * (TVW + 12) && x < 16 + i * (TVW + 12) + TVW) return i;
	return -1;
}

static void tv_pick(int i)
{
	int ws = tv_ws[i];
	if (ws < 0) {
		int used = 0;
		for (Client *c = clients; c; c = c->next) used |= 1 << c->ws;
		for (ws = 0; ws < NWS && (ws == curws || ((used >> ws) & 1)); ws++);
		if (ws == NWS) return;
	}
	tv_close();
	ws_view(ws);
}

static void close_popups(void)
{
	tv_close();
	ctx_close();
	sm_close();
	qs_close();
	at_end(0);
}

/* ---------- misc actions ---------- */

static void show_desktop(void)
{
	int any = 0;
	for (Client *c = clients; c; c = c->next) if (!c->minimized && !c->skiptb && c->ws == curws) any = 1;
	if (any) {
		sd_n = 0;
		for (Client *c = clients; c; c = c->next)
			if (!c->minimized && !c->skiptb && c->ws == curws) { if (sd_n < (int)LEN(sd_list)) sd_list[sd_n++] = c; minimize(c); }
	} else {
		for (int i = 0; i < sd_n; i++) activate(sd_list[i]);
		sd_n = 0;
	}
}

static void wallpaper(void)
{
	char cmd[700];
	struct stat st;
	XSetWindowBackground(dpy, root, th.desk);
	XClearWindow(dpy, root);
	if (cfg.wallpaper[0] && !stat(cfg.wallpaper, &st) && !strchr(cfg.wallpaper, '\'') && have("xwallpaper")) {
		snprintf(cmd, sizeof cmd, "xwallpaper --zoom '%s'", cfg.wallpaper);
		spawn(cmd);
	}
}

static void screenshot(int region)
{
	if (have("glue-action")) spawn(region ? "glue-action screenshot-region" : "glue-action screenshot");
	else if (have("maim")) spawn(region ? "maim -s | xclip -selection clipboard -t image/png"
		: "mkdir -p ~/Pictures/Screenshots && maim ~/Pictures/Screenshots/$(date +%Y%m%d-%H%M%S).png");
	else if (have("import")) spawn("mkdir -p ~/Pictures/Screenshots && import -window root ~/Pictures/Screenshots/$(date +%Y%m%d-%H%M%S).png");
}

/* ---------- monitors ---------- */

static void setup_bars(void)
{
	XSetWindowAttributes swa;
	for (int i = 0; i < nmons; i++) {
		swa.override_redirect = True;
		swa.background_pixel = th.bar;
		swa.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask | ExposureMask;
		if (!mons[i].bar)
			mons[i].bar = XCreateWindow(dpy, root, 0, 0, 1, 1, 0, depth, InputOutput, vis,
				CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
		XSetWindowBackground(dpy, mons[i].bar, th.bar);
		XMoveResizeWindow(dpy, mons[i].bar, mons[i].x, cfg.bar_top ? mons[i].y : mons[i].y + mons[i].h - TBH, mons[i].w, TBH);
		XMapWindow(dpy, mons[i].bar);
	}
}

static void update_mons(void)
{
	int n = 0, i, old = nmons;
	XRRMonitorInfo *mi_ = XRRGetMonitors(dpy, root, True, &n);
	Mon nm[MAXMON] = {0};
	int k = 0;
	if (mi_) {
		for (i = 0; i < n && k < MAXMON; i++)
			if (mi_[i].primary) nm[k++] = (Mon){mi_[i].x, mi_[i].y, mi_[i].width, mi_[i].height, 0};
		for (i = 0; i < n && k < MAXMON; i++)
			if (!mi_[i].primary) nm[k++] = (Mon){mi_[i].x, mi_[i].y, mi_[i].width, mi_[i].height, 0};
		XRRFreeMonitors(mi_);
	}
	if (!k) nm[k++] = (Mon){0, 0, DisplayWidth(dpy, scr), DisplayHeight(dpy, scr), 0};
	for (i = 0; i < MAXMON; i++) nm[i].bar = i < old ? mons[i].bar : 0;
	if (ntray && k < 1) tray_release();
	for (i = k; i < old; i++) if (mons[i].bar) XDestroyWindow(dpy, mons[i].bar);
	memcpy(mons, nm, sizeof mons);
	nmons = k;
	setup_bars();
	for (Client *c = clients; c; c = c->next) {
		int m = mon_of(c), x, y, w, h;
		workarea(m, &x, &y, &w, &h);
		if (c->full) { c->x = mons[m].x; c->y = mons[m].y; c->w = mons[m].w; c->h = mons[m].h; }
		else if (c->max) { c->x = x; c->y = y; c->w = w; c->h = h; }
		else if (c->snap) { set_snap(c, c->snap); continue; }
		else { c->x = CLAMP(c->x, x, MAX(x, x + w - c->w)); c->y = CLAMP(c->y, y, MAX(y, y + h - c->h)); }
		apply(c);
	}
	ewmh_workarea();
	arrange();
	restack();
	tb_draw_all();
}

/* ---------- keys ---------- */

static unsigned numlock;

static void grab_key(KeySym ks, unsigned mod)
{
	KeyCode kc = XKeysymToKeycode(dpy, ks);
	unsigned mods[] = {0, LockMask, numlock, numlock | LockMask};
	if (!kc) return;
	for (int i = 0; i < 4; i++) XGrabKey(dpy, kc, mod | mods[i], root, True, GrabModeAsync, GrabModeAsync);
}

static void grab_keys(void)
{
	XModifierKeymap *mm = XGetModifierMapping(dpy);
	KeyCode nl = XKeysymToKeycode(dpy, XK_Num_Lock);
	numlock = 0;
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < mm->max_keypermod; j++)
			if (nl && mm->modifiermap[i * mm->max_keypermod + j] == nl) numlock = 1u << i;
	XFreeModifiermap(mm);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Super_L), AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
	XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Super_R), AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
	grab_key(XK_Tab, Mod1Mask);
	grab_key(XK_Tab, Mod1Mask | ShiftMask);
	grab_key(XK_F4, Mod1Mask);
	grab_key(XK_Escape, ControlMask | ShiftMask);
	grab_key(XK_Delete, ControlMask | Mod1Mask);
	grab_key(XK_Print, 0);
	static const KeySym media[] = {XF86XK_AudioRaiseVolume, XF86XK_AudioLowerVolume, XF86XK_AudioMute,
		XF86XK_MonBrightnessUp, XF86XK_MonBrightnessDown, XF86XK_AudioPlay, XF86XK_AudioNext, XF86XK_AudioPrev};
	for (int i = 0; i < (int)LEN(media); i++) grab_key(media[i], 0);
}

static void ws_sync(Client *c)
{
	if (c->ws == curws && !c->minimized) { XMapWindow(dpy, c->frame); place_handles(c); }
	else { XUnmapWindow(dpy, c->frame); for (int i = 0; i < 4; i++) XUnmapWindow(dpy, c->hdl[i]); }
}

static void ws_view(int n)
{
	long d = n;
	if (n < 0 || n >= NWS || n == curws) return;
	close_popups();
	curws = n;
	for (Client *c = clients; c; c = c->next) ws_sync(c);
	XChangeProperty(dpy, root, A[NetCurDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&d, 1);
	arrange();
	focus_top();
}

static void ws_move(Client *c, int n)
{
	long d = n;
	if (n < 0 || n >= NWS || c->ws == n) return;
	c->ws = n;
	c->tord = ++tordctr;
	XChangeProperty(dpy, c->win, A[NetWMDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&d, 1);
	ws_sync(c);
	arrange();
	if (sel == c) focus_top();
	else tb_draw_all();
}

static void super_key(KeySym ks, unsigned st)
{
	Client *c = sel;
	if (ks >= XK_1 && ks <= XK_9) {
		if (st & ShiftMask) { if (c) ws_move(c, (int)(ks - XK_1)); }
		else ws_view((int)(ks - XK_1));
		return;
	}
	if ((st & ControlMask) && (ks == XK_Left || ks == XK_Right)) { ws_view(curws + (ks == XK_Left ? -1 : 1)); return; }
	if (c && is_tiled(c) && (ks == XK_Left || ks == XK_Right)) { tile_swap(c, ks == XK_Left ? -1 : 1); return; }
	switch (ks) {
	case XK_Tab: if (tv_on) tv_close(); else tv_open(pointer_mon()); break;
	case XK_space: if (nkbl > 1) XkbLockGroup(dpy, XkbUseCoreKbd, (unsigned)((kbd_group() + (st & ShiftMask ? nkbl - 1 : 1)) % nkbl)); break;
	case XK_t: { int on = !tilews[curws]; for (int i = 0; i < NWS; i++) tilews[i] = on; arrange(); tb_draw_all(); break; }
	case XK_f:
		if (!c) break;
		c->floating = !c->floating;
		arrange();
		if (c->floating) raise_client(c);
		break;
	case XK_d: close_popups(); show_desktop(); break;
	case XK_m: close_popups(); for (Client *k = clients; k; k = k->next) if (!k->skiptb && k->ws == curws) minimize(k); break;
	case XK_e: close_popups(); spawn(cfg.files); break;
	case XK_r: sm_open(3, pointer_mon()); break;
	case XK_s:
		if (st & ShiftMask) { close_popups(); screenshot(1); }
		else sm_open(3, pointer_mon());
		break;
	case XK_q: sm_open(3, pointer_mon()); break;
	case XK_l: close_popups(); lock_screen(); break;
	case XK_i: close_popups(); spawn("gluewin-settings"); break;
	case XK_a: if (qs_on) qs_close(); else qs_open(); break;
	case XK_Return: close_popups(); spawn(cfg.terminal); break;
	case XK_Up: if (c) { if (c->snap) set_max(c, 0); set_max(c, 1); } break;
	case XK_Down:
		if (!c) break;
		if (c->max || c->snap) set_max(c, 0);
		else minimize(c);
		break;
	case XK_Left: if (c) { if (c->snap == 2) set_max(c, 0); else set_snap(c, 1); } break;
	case XK_Right: if (c) { if (c->snap == 1) set_max(c, 0); else set_snap(c, 2); } break;
	}
}

static void keypress(XKeyEvent *e)
{
	KeySym ks = XLookupKeysym(e, 0);
	unsigned st = e->state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
	if (at_on) {
		if (ks == XK_Tab || ks == XK_Right) at_sel = (at_sel + ((st & ShiftMask) ? at_n - 1 : 1)) % at_n;
		else if (ks == XK_ISO_Left_Tab || ks == XK_Left) at_sel = (at_sel + at_n - 1) % at_n;
		else if (ks == XK_Escape) { at_end(0); return; }
		else if (ks == XK_Return) { at_end(1); return; }
		at_draw();
		return;
	}
	if (ks == XK_Super_L || ks == XK_Super_R) { super_alone = 1; return; }
	super_alone = 0;
	if (st & Mod4Mask) { super_key(ks, st); return; }
	if (sm_on && !ctx_on) { sm_key(e); return; }
	if (ctx_on || qs_on || tv_on) {
		if (ks == XK_Escape) { if (ctx_on) ctx_close(); else if (tv_on) tv_close(); else qs_close(); }
		return;
	}
	if ((st & Mod1Mask) && ks == XK_Tab) { at_start(st & ShiftMask); return; }
	if ((st & Mod1Mask) && ks == XK_F4) {
		if (sel) close_client(sel);
		else { power_menu_items(); ctx_kind = CX_POWER; ctx_show(mons[0].x + mons[0].w / 2 - 100, mons[0].y + mons[0].h / 3, 0); }
		return;
	}
	if ((st & (ControlMask | ShiftMask)) == (ControlMask | ShiftMask) && ks == XK_Escape) { task_manager(); return; }
	if ((st & (ControlMask | Mod1Mask)) == (ControlMask | Mod1Mask) && ks == XK_Delete) {
		power_menu_items();
		ctx_add("", -1);
		ctx_add(T("Task Manager", "Manager de activități"), 6);
		ctx_kind = CX_CAD;
		ctx_show(mons[0].x + mons[0].w / 2 - 100, mons[0].y + mons[0].h / 3, 0);
		return;
	}
	switch (ks) {
	case XK_Print: screenshot(0); break;
	case XF86XK_AudioRaiseVolume: spawn(have("wpctl") ? "wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+" : "pactl set-sink-volume @DEFAULT_SINK@ +5%"); break;
	case XF86XK_AudioLowerVolume: spawn(have("wpctl") ? "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-" : "pactl set-sink-volume @DEFAULT_SINK@ -5%"); break;
	case XF86XK_AudioMute: spawn(have("wpctl") ? "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle" : "pactl set-sink-mute @DEFAULT_SINK@ toggle"); break;
	case XF86XK_MonBrightnessUp: spawn("brightnessctl -q set 5%+"); break;
	case XF86XK_MonBrightnessDown: spawn("brightnessctl -q set 5%-"); break;
	case XF86XK_AudioPlay: spawn("playerctl play-pause"); break;
	case XF86XK_AudioNext: spawn("playerctl next"); break;
	case XF86XK_AudioPrev: spawn("playerctl previous"); break;
	}
}

static void keyrelease(XKeyEvent *e)
{
	KeySym ks = XLookupKeysym(e, 0);
	if ((ks == XK_Super_L || ks == XK_Super_R) && super_alone) {
		super_alone = 0;
		if (sm_on) sm_close();
		else sm_open(0, pointer_mon());
		return;
	}
	if (at_on && (ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L)) at_end(1);
}

/* ---------- drag ---------- */

static void snap_preview(int zone, int m)
{
	int x, y, w, h;
	XRectangle r[4];
	if (!zone) { if (snapwin) XUnmapWindow(dpy, snapwin); return; }
	workarea(m, &x, &y, &w, &h);
	if (zone == 1) w /= 2;
	else if (zone == 2) { x += w / 2; w -= w / 2; }
	x += 6; y += 6; w -= 12; h -= 12;
	if (!snapwin) {
		XSetWindowAttributes swa;
		swa.override_redirect = True;
		swa.background_pixel = th.accent;
		snapwin = XCreateWindow(dpy, root, 0, 0, 1, 1, 0, depth, InputOutput, vis, CWOverrideRedirect | CWBackPixel, &swa);
	}
	XSetWindowBackground(dpy, snapwin, th.accent);
	XMoveResizeWindow(dpy, snapwin, x, y, w, h);
	r[0] = (XRectangle){0, 0, (unsigned short)w, 4};
	r[1] = (XRectangle){0, (short)(h - 4), (unsigned short)w, 4};
	r[2] = (XRectangle){0, 0, 4, (unsigned short)h};
	r[3] = (XRectangle){(short)(w - 4), 0, 4, (unsigned short)h};
	XShapeCombineRectangles(dpy, snapwin, ShapeBounding, 0, 0, r, 4, ShapeSet, Unsorted);
	XMapRaised(dpy, snapwin);
	XClearWindow(dpy, snapwin);
}

static int dir_cursor(int d)
{
	switch (d) {
	case DL: return CUR_L; case DR: return CUR_R; case DT: return CUR_T; case DB: return CUR_B;
	case DT | DL: return CUR_TL; case DT | DR: return CUR_TR; case DB | DL: return CUR_BL; case DB | DR: return CUR_BR;
	}
	return CUR_MOVE;
}

static void drag_start(Client *c, int mode, int dir, int px, int py)
{
	if (!c || c->full) return;
	close_popups();
	if (XGrabPointer(dpy, root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
		None, cur[mode ? dir_cursor(dir) : CUR_NORMAL], CurrentTime) != GrabSuccess) return;
	drag = (typeof(drag)){1, mode, dir, px, py, c->x, c->y, c->w, c->h, 0, 0, c};
}

static void drag_motion(int px, int py)
{
	Client *c = drag.c;
	int dx = px - drag.px, dy = py - drag.py, m;
	if (!c) return;
	if (!drag.moved && abs(dx) < 4 && abs(dy) < 4) return;
	if (!drag.moved && is_tiled(c)) { c->floating = 1; c->tiled = 0; arrange(); }
	if (drag.mode == 0) {
		if (!drag.moved && (c->max || c->snap)) {
			/* tearing a maximized window off keeps the cursor at the same spot of the title */
			int rel = (px - c->x) * c->ow / MAX(c->w, 1);
			c->max = c->snap = 0;
			c->w = c->ow; c->h = c->oh;
			c->x = px - rel; c->y = py - TH / 2;
			drag.ox = c->x; drag.oy = c->y; drag.px = px; drag.py = py;
			dx = dy = 0;
			apply(c);
			ewmh_state(c);
		}
		drag.moved = 1;
		c->x = drag.ox + dx;
		c->y = drag.oy + dy;
		m = mon_at(px, py);
		c->y = MAX(c->y, mons[m].y + (cfg.bar_top ? TBH : 0) - 4);
		move_only(c);
		int zone = px <= mons[m].x + 1 ? 1 : px >= mons[m].x + mons[m].w - 2 ? 2 : py <= mons[m].y + 1 ? 3 : 0;
		if (zone != drag.zone) { drag.zone = zone; snap_preview(zone, m); }
	} else {
		drag.moved = 1;
		int x = drag.ox, y = drag.oy, w = drag.ow, h = drag.oh;
		if (drag.dir & DL) { w = MAX(drag.ow - dx, c->minw); x = drag.ox + drag.ow - w; }
		if (drag.dir & DR) w = MAX(drag.ow + dx, c->minw);
		if (drag.dir & DT) { h = MAX(drag.oh - dy, c->minh + TH); y = drag.oy + drag.oh - h; }
		if (drag.dir & DB) h = MAX(drag.oh + dy, c->minh + TH);
		c->x = x; c->y = y; c->w = w; c->h = h;
		apply(c);
	}
}

static void drag_end(int px, int py)
{
	Client *c = drag.c;
	int zone = drag.zone;
	drag.on = 0;
	drag.c = NULL;
	XUngrabPointer(dpy, CurrentTime);
	if (snapwin) XUnmapWindow(dpy, snapwin);
	if (!c) return;
	if (drag.mode == 0 && zone) {
		int m = mon_at(px, py);
		c->x = CLAMP(c->x, mons[m].x, mons[m].x + mons[m].w - 40);
		c->ox = c->x; c->oy = MAX(c->y, mons[m].y);
		if (zone == 3) set_max(c, 1);
		else set_snap(c, zone);
	} else if (drag.mode == 0) send_configure(c);
	else c->snap = 0;
}

/* ---------- events ---------- */

static int in_popup(Window w)
{
	return (sm_on && w == smwin) || (ctx_on && w == ctxwin) || (qs_on && w == qswin) || (at_on && w == atwin) || (tv_on && w == tvwin);
}

static void tb_ctx(int x, int m)
{
	if (cfg.bar_top) ctx_show(x, mons[m].y + TBH + 8, 0);
	else ctx_show(x, mons[m].y + mons[m].h - TBH - 8, 1);
}

static void tb_press(int m, XButtonEvent *e)
{
	int i = tb_hit(m, e->x);
	TbItem it;
	if (i < 0) {
		if (e->button == 3) {
			nmi = 0;
			ctx_kind = CX_TASKBAR;
			ctx_add(T("Task Manager", "Manager de activități"), 1);
			ctx_add(T("Taskbar settings", "Setări bară de activități"), 2);
			tb_ctx(e->x_root, m);
		}
		return;
	}
	it = tbi[i];
	switch (it.kind) {
	case TB_START: if (sm_on) sm_close(); else sm_open(0, m); break;
	case TB_SEARCH: sm_open(3, m); break;
	case TB_SYS: if (qs_on) qs_close(); else qs_open(); break;
	case TB_DESK: close_popups(); show_desktop(); break;
	case TB_LANG: {
		int g = kbd_group();
		char l[96];
		close_popups();
		nmi = 0;
		ctx_kind = CX_LANG;
		for (int k = 0; k < nkbl; k++) {
			const Kbd *kb = kbd_info(kbl[k]);
			const char *a, *b;
			kbd_label(k, &a, &b);
			snprintf(l, sizeof l, "%s  %s %s  %s", k == g ? "•" : "  ", a, b, kb ? T(kb->en, kb->ro) : kbl[k]);
			ctx_add(l, 10 + k);
		}
		ctx_add("", -1);
		ctx_add(T("More keyboard settings", "Mai multe setări pentru tastatură"), 1);
		tb_ctx(it.x + mons[m].x, m);
		break;
	}
	case TB_TASKVIEW: if (tv_on) tv_close(); else tv_open(m); break;
	case TB_GROUP: {
		Group *g = &G[it.g];
		if (e->button == 2 && g->app) { app_launch(g->app); break; }
		if (e->button == 3) {
			int k;
			nmi = 0;
			ctx_kind = CX_GROUP;
			ctx_app = g->app;
			memset(ctx_wins, 0, sizeof ctx_wins);
			for (k = 0; k < g->n && k < (int)LEN(ctx_wins) - 1; k++) {
				ctx_wins[k] = g->w[k]->win;
				ctx_add(g->w[k]->title[0] ? g->w[k]->title : g->w[k]->cls, 100 + k);
			}
			if (k) ctx_add("", -1);
			if (g->app) {
				ctx_add(g->app->name, 1);
				ctx_add(g->pinned ? T("Unpin from taskbar", "Anulează fixarea din bara de activități")
					: T("Pin to taskbar", "Fixează în bara de activități"), 2);
			}
			if (g->n) ctx_add(g->n > 1 ? T("Close all windows", "Închide toate ferestrele") : T("Close window", "Închide fereastra"), 3);
			tb_ctx(mons[m].x + it.x, m);
			break;
		}
		close_popups();
		if (!g->n) { if (g->app) app_launch(g->app); break; }
		if (g->n == 1) {
			if (sel == g->w[0]) minimize(g->w[0]);
			else activate(g->w[0]);
			break;
		}
		int k, cur_ = -1;
		for (k = 0; k < g->n; k++) if (g->w[k] == sel) cur_ = k;
		if (cur_ < 0) {
			Client *best = g->w[0];
			for (k = 1; k < g->n; k++) if (g->w[k]->stamp > best->stamp) best = g->w[k];
			activate(best);
		} else activate(g->w[(cur_ + 1) % g->n]);
		break;
	}
	}
}

static void frame_press(Client *c, XButtonEvent *e)
{
	int b = btn_at(c, e->x, e->y);
	if (sel != c || c->minimized) activate(c);
	else raise_client(c);
	if (e->y >= c_th(c) && !b) {
		int d = 0;
		if (e->x < 8) d |= DL;
		if (e->x >= c->w - 8) d |= DR;
		if (e->y >= c->h - 8) d |= DB;
		if (d && e->button == 1 && resizable(c)) drag_start(c, 1, d, e->x_root, e->y_root);
		return;
	}
	if (e->button == 3 && !b) {
		nmi = 0;
		ctx_kind = CX_WIN;
		ctx_win1 = c->win;
		if (c->max || c->snap) ctx_add(T("Restore", "Restabilire"), 1);
		ctx_add(T("Minimize", "Minimizare"), 2);
		if (!c->max && !c->dialog) ctx_add(T("Maximize", "Maximizare"), 3);
		ctx_add("", -1);
		ctx_add(T("Close        Alt+F4", "Închidere        Alt+F4"), 4);
		ctx_show(e->x_root, e->y_root, 0);
		return;
	}
	if (e->button != 1) return;
	if (b) { c->press = b; c->hover = b; draw_frame(c); return; }
	if (e->y < 5 && !c->max && resizable(c)) { drag_start(c, 1, DT | (e->x < 12 ? DL : e->x > c->w - 12 ? DR : 0), e->x_root, e->y_root); return; }
	if (last_click_c == c && e->time - last_click < 400 && !c->dialog) {
		last_click_c = NULL;
		set_max(c, !(c->max || c->snap));
		return;
	}
	last_click = e->time;
	last_click_c = c;
	drag_start(c, 0, 0, e->x_root, e->y_root);
}

static void frame_release(Client *c, XButtonEvent *e)
{
	int b = btn_at(c, e->x, e->y), p = c->press;
	c->press = 0;
	draw_frame(c);
	if (!p || p != b) return;
	if (b == 1) minimize(c);
	else if (b == 2) set_max(c, !(c->max || c->snap));
	else if (b == 3) close_client(c);
}

static void handle_press(Client *c, int side, XButtonEvent *e)
{
	int d = side == 0 ? DT : side == 1 ? DB : side == 2 ? DL : DR;
	if (side < 2) { if (e->x < 18) d |= DL; else if (e->x >= c->w + 2 * HDL - 18) d |= DR; }
	else { if (e->y < 18) d |= DT; else if (e->y >= c->h - 18) d |= DB; }
	activate(c);
	drag_start(c, 1, d, e->x_root, e->y_root);
}

static void buttonpress(XButtonEvent *e)
{
	Client *c;
	int m, side;
	if (drag.on) return;
	if ((sm_on || ctx_on || qs_on || tv_on) && !in_popup(e->window)) {
		int was_start = sm_on, was_qs = qs_on, was_tv = tv_on;
		if (ctx_on && !(sm_on && e->window == smwin)) { ctx_close(); if (sm_on && e->window == smwin) goto popup; }
		m = monbar(e->window);
		if (m >= 0) {
			int i = tb_hit(m, e->x);
			if (i >= 0 && ((tbi[i].kind == TB_START && was_start) || (tbi[i].kind == TB_SYS && was_qs) || (tbi[i].kind == TB_TASKVIEW && was_tv))) { close_popups(); return; }
		}
		close_popups();
		if (m < 0 && e->window == root) return;
	}
popup:
	if (sm_on && e->window == smwin) {
		if (ctx_on) { ctx_close(); return; }
		if (e->button == 4 || e->button == 5) {
			if (sm_mode == 1 || sm_q[0]) {
				sm_scroll = MAX(sm_scroll + (e->button == 5 ? 3 : -3), 0);
				if (sm_q[0]) sm_sel = CLAMP(sm_sel + (e->button == 5 ? 3 : -3), 0, MAX(sm_nres - 1, 0));
				sm_draw();
			}
			return;
		}
		for (int i = 0; i < nsmh; i++)
			if (e->x >= smh[i].x && e->x < smh[i].x + smh[i].w && e->y >= smh[i].y && e->y < smh[i].y + smh[i].h) {
				sm_hit(&smh[i], e->button, e->x_root, e->y_root);
				return;
			}
		return;
	}
	if (ctx_on && e->window == ctxwin) { int i = ctx_hit(e->y); if (i >= 0 && e->button == 1) ctx_activate(i); return; }
	if (qs_on && e->window == qswin) { if (e->button == 1) qs_press(e->x, e->y); return; }
	if (tv_on && e->window == tvwin) { int i = tv_hit(e->x, e->y); if (i >= 0 && e->button == 1) tv_pick(i); return; }
	if ((c = bywin(e->window))) {
		activate(c);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		return;
	}
	if ((c = byframe(e->window))) { frame_press(c, e); return; }
	if ((c = byhandle(e->window, &side))) { if (e->button == 1) handle_press(c, side, e); return; }
	if ((m = monbar(e->window)) >= 0) { tb_press(m, e); return; }
	if (e->window == root && e->button == 3) {
		nmi = 0;
		ctx_kind = CX_DESK;
		ctx_add(T("Personalize", "Personalizare"), 1);
		ctx_add(T("Display settings", "Setări afișaj"), 2);
		ctx_add("", -1);
		ctx_add(T("Open in Terminal", "Deschide în terminal"), 3);
		ctx_show(e->x_root, e->y_root, 0);
	}
}

static void motion(XMotionEvent *e)
{
	Client *c;
	int m;
	XEvent ev;
	while (XCheckTypedEvent(dpy, MotionNotify, &ev)) e = &ev.xmotion;
	if (drag.on) { drag_motion(e->x_root, e->y_root); return; }
	if (qs_on && qs_drag) {
		int v = CLAMP((e->x_root - (int)(mons[0].x + mons[0].w - QSW - 12) - 52) * 100 / (QSW - 52 - 56), 0, 100);
		qs_apply(qs_drag, v, 0);
		return;
	}
	if (sm_on && e->window == smwin) {
		int h = -1;
		for (int i = 0; i < nsmh; i++)
			if (smh[i].kind != H_SEARCH && e->x >= smh[i].x && e->x < smh[i].x + smh[i].w && e->y >= smh[i].y && e->y < smh[i].y + smh[i].h) h = i;
		if (h != sm_hover) { sm_hover = h; sm_draw(); }
		return;
	}
	if (tv_on && e->window == tvwin) {
		int h = tv_hit(e->x, e->y);
		if (h != tv_hover) { tv_hover = h; tv_draw(); }
		return;
	}
	if (ctx_on && e->window == ctxwin) {
		int h = ctx_hit(e->y);
		if (h != ctx_hover) { ctx_hover = h; ctx_draw(); }
		return;
	}
	if (qs_on && e->window == qswin) {
		int v, h = qs_hit(e->x, e->y, &v);
		if (h == QS_BRI || h == QS_VOL) h = -1;
		if (h != qs_hover) { qs_hover = h; qs_draw(); }
		return;
	}
	if ((m = monbar(e->window)) >= 0) {
		int h = tb_hit(m, e->x);
		if (h != tb_hover || m != tb_hmon) { tb_hover = h; tb_hmon = m; tb_draw(m); }
		return;
	}
	if ((c = byframe(e->window))) {
		int b = btn_at(c, e->x, e->y);
		if (b != c->hover) { c->hover = b; draw_frame(c); }
		XDefineCursor(dpy, c->frame, e->y >= c_th(c) && resizable(c) ?
			cur[dir_cursor((e->x < 8 ? DL : e->x >= c->w - 8 ? DR : 0) | (e->y >= c->h - 8 ? DB : 0))] :
			e->y < 5 && resizable(c) && !b ? cur[CUR_T] : cur[CUR_NORMAL]);
		return;
	}
	if ((c = byhandle(e->window, &m))) {
		int d = m == 0 ? DT : m == 1 ? DB : m == 2 ? DL : DR;
		if (m < 2) { if (e->x < 18) d |= DL; else if (e->x >= c->w + 2 * HDL - 18) d |= DR; }
		else { if (e->y < 18) d |= DT; else if (e->y >= c->h - 18) d |= DB; }
		XDefineCursor(dpy, e->window, cur[dir_cursor(d)]);
	}
}

static void buttonrelease(XButtonEvent *e)
{
	Client *c;
	if (drag.on) { drag_end(e->x_root, e->y_root); return; }
	if (qs_drag) {
		int v = CLAMP((e->x_root - (int)(mons[0].x + mons[0].w - QSW - 12) - 52) * 100 / (QSW - 52 - 56), 0, 100);
		qs_apply(qs_drag, v, 1);
		qs_drag = 0;
		return;
	}
	if ((c = byframe(e->window))) frame_release(c, e);
}

static void leave(XCrossingEvent *e)
{
	Client *c;
	int m;
	if ((c = byframe(e->window)) && c->hover && !c->press) { c->hover = 0; draw_frame(c); }
	else if ((m = monbar(e->window)) >= 0 && tb_hover >= 0) { tb_hover = -1; tb_draw(m); }
	else if (sm_on && e->window == smwin && sm_hover >= 0) { sm_hover = -1; sm_draw(); }
	else if (ctx_on && e->window == ctxwin && ctx_hover >= 0) { ctx_hover = -1; ctx_draw(); }
	else if (qs_on && e->window == qswin && qs_hover >= 0) { qs_hover = -1; qs_draw(); }
	else if (tv_on && e->window == tvwin && tv_hover >= 0) { tv_hover = -1; tv_draw(); }
}

static void clientmessage(XClientMessageEvent *e)
{
	Client *c = bywin(e->window);
	if (e->window == trayselwin && e->message_type == A[TrayOpcode]) {
		if (e->data.l[1] == 0) tray_dock((Window)e->data.l[2]);
		return;
	}
	if (e->message_type == A_RELOAD) { reload(); return; }
	if (e->message_type == A[NetCurDesktop]) { ws_view((int)e->data.l[0]); return; }
	if (!c) return;
	if (e->message_type == A[NetWMDesktop]) { ws_move(c, (int)e->data.l[0]); return; }
	if (e->message_type == A[NetActiveWindow]) activate(c);
	else if (e->message_type == A[NetCloseWindow]) close_client(c);
	else if (e->message_type == A[WMChangeState] && e->data.l[0] == IconicState) minimize(c);
	else if (e->message_type == A[NetWMState]) {
		for (int i = 1; i <= 2; i++) {
			Atom a = (Atom)e->data.l[i];
			int act = (int)e->data.l[0];
			if (a == A[NetWMFullscreen]) set_full(c, act == 1 || (act == 2 && !c->full));
			else if (a == A[NetWMMaxV] || a == A[NetWMMaxH]) {
				int on = act == 1 || (act == 2 && !c->max);
				if (on != c->max) set_max(c, on);
				break;
			}
		}
	} else if (e->message_type == A[NetMoveResize]) {
		static const int dirs[8] = {DT | DL, DT, DT | DR, DR, DB | DR, DB, DB | DL, DL};
		long d = e->data.l[2];
		if (d == 8) drag_start(c, 0, 0, (int)e->data.l[0], (int)e->data.l[1]);
		else if (d >= 0 && d < 8 && resizable(c)) drag_start(c, 1, dirs[d], (int)e->data.l[0], (int)e->data.l[1]);
		else if (d == 11 && drag.on) drag_end((int)e->data.l[0], (int)e->data.l[1]);
	}
}

static void configurerequest(XConfigureRequestEvent *e)
{
	Client *c = bywin(e->window);
	if (c) {
		int bw = c_bw(c), th_ = c_th(c);
		if (c->full || c->max || c->snap || is_tiled(c)) { send_configure(c); return; }
		if (e->value_mask & CWWidth) c->w = e->width + 2 * bw;
		if (e->value_mask & CWHeight) c->h = e->height + th_ + bw;
		if (e->value_mask & CWX) c->x = e->x - bw;
		if (e->value_mask & CWY) c->y = e->y - th_;
		apply(c);
		return;
	}
	XWindowChanges wc = {e->x, e->y, e->width, e->height, e->border_width, e->above, e->detail};
	XConfigureWindow(dpy, e->window, (unsigned)e->value_mask, &wc);
}

static void propertynotify(XPropertyEvent *e)
{
	Client *c = bywin(e->window);
	if (e->window == root && e->atom == XInternAtom(dpy, "_XKB_RULES_NAMES", False)) { kbd_refresh(); update_mons(); return; }
	if (!c || e->state == PropertyDelete) return;
	if (e->atom == XA_WM_NAME || e->atom == A[NetWMName]) { get_title(c); draw_frame(c); if (at_on) at_draw(); }
	else if (e->atom == A[NetWMIcon]) { get_icon(c); draw_frame(c); tb_draw_all(); }
	else if (e->atom == XA_WM_HINTS || e->atom == XA_WM_NORMAL_HINTS) get_hints(c);
}

static void expose(XExposeEvent *e)
{
	Client *c;
	int m;
	if (e->count) return;
	if ((c = byframe(e->window))) draw_frame(c);
	else if ((m = monbar(e->window)) >= 0) tb_draw(m);
	else if (sm_on && e->window == smwin) sm_draw();
	else if (ctx_on && e->window == ctxwin) ctx_draw();
	else if (qs_on && e->window == qswin) qs_draw();
	else if (at_on && e->window == atwin) at_draw();
	else if (tv_on && e->window == tvwin) tv_draw();
}

static void reload(void)
{
	cfg_load();
	theme_apply();
	apps_load();
	for (Client *c = clients; c; c = c->next) {
		c->app = app_for_class(c->cls, c->inst);
		c->shr = -1;
		apply(c);
	}
	for (int i = 0; i < 5; i++) {
		Window w[5] = {smwin, ctxwin, qswin, atwin, tvwin};
		if (w[i]) XSetWindowBackground(dpy, w[i], th.menu);
	}
	if (cfg.tiling != last_tiling) {
		for (int i = 0; i < NWS; i++) tilews[i] = cfg.tiling;
		last_tiling = cfg.tiling;
	}
	update_mons();
	wallpaper();
	kbd_apply();
	if (sm_on) sm_draw();
	if (qs_on) qs_draw();
}

static int rr_event;

static void handle(XEvent *ev)
{
	Client *c;
	switch (ev->type) {
	case MapRequest: {
		XWindowAttributes wa;
		if (!XGetWindowAttributes(dpy, ev->xmaprequest.window, &wa) || wa.override_redirect) return;
		if ((c = bywin(ev->xmaprequest.window))) { activate(c); return; }
		manage(ev->xmaprequest.window, &wa);
		return;
	}
	case UnmapNotify:
		if ((c = bywin(ev->xunmap.window))) {
			if (ev->xunmap.event != c->frame && !ev->xunmap.send_event) return;
			if (c->ignore_unmap > 0 && !ev->xunmap.send_event) { c->ignore_unmap--; return; }
			unmanage(c, 0);
		}
		return;
	case DestroyNotify:
		if ((c = bywin(ev->xdestroywindow.window))) unmanage(c, 1);
		else if (tray_index(ev->xdestroywindow.window) >= 0) tray_remove(ev->xdestroywindow.window);
		else for (int i = 0; i < ndocks; i++)
			if (docks[i] == ev->xdestroywindow.window) { docks[i] = docks[--ndocks]; break; }
		return;
	case ReparentNotify:
		if (tray_index(ev->xreparent.window) >= 0 && ev->xreparent.parent != mons[0].bar) tray_remove(ev->xreparent.window);
		return;
	case ConfigureRequest: configurerequest(&ev->xconfigurerequest); return;
	case ClientMessage: clientmessage(&ev->xclient); return;
	case PropertyNotify: propertynotify(&ev->xproperty); return;
	case ButtonPress: buttonpress(&ev->xbutton); return;
	case ButtonRelease: buttonrelease(&ev->xbutton); return;
	case MotionNotify: motion(&ev->xmotion); return;
	case LeaveNotify: leave(&ev->xcrossing); return;
	case KeyPress: keypress(&ev->xkey); return;
	case KeyRelease: keyrelease(&ev->xkey); return;
	case Expose: expose(&ev->xexpose); return;
	case MappingNotify:
		XRefreshKeyboardMapping(&ev->xmapping);
		if (ev->xmapping.request == MappingKeyboard) grab_keys();
		return;
	case SelectionClear:
		if (ev->xselectionclear.window == trayselwin) tray_release();
		return;
	}
	if (ev->type == xkb_event) { tb_draw_all(); return; }
	if (ev->type == rr_event + RRScreenChangeNotify) {
		XRRUpdateConfiguration(ev);
		update_mons();
		wallpaper();
	}
}

/* ---------- startup ---------- */

static void autostart(void)
{
	char dirs[2][300], p[700], line[1024], *v, done[64][128];
	int nd = 0;
	snprintf(dirs[0], sizeof dirs[0], "%s/.config/autostart", home());
	snprintf(dirs[1], sizeof dirs[1], "/etc/xdg/autostart");
	for (int i = 0; i < 2; i++) {
		DIR *d = opendir(dirs[i]);
		struct dirent *e;
		if (!d) continue;
		while ((e = readdir(d))) {
			char exec[600] = "";
			int skip = 0, k;
			FILE *f;
			if (!strstr(e->d_name, ".desktop")) continue;
			for (k = 0; k < nd && strcmp(done[k], e->d_name); k++);
			if (k < nd) continue;
			if (nd < 64) snprintf(done[nd++], 128, "%s", e->d_name);
			snprintf(p, sizeof p, "%s/%s", dirs[i], e->d_name);
			if (!(f = fopen(p, "r"))) continue;
			while (fgets(line, sizeof line, f)) {
				line[strcspn(line, "\r\n")] = 0;
				if (!(v = strchr(line, '='))) continue;
				*v++ = 0;
				if (!strcmp(line, "Exec")) snprintf(exec, sizeof exec, "%s", v);
				else if (!strcmp(line, "Hidden") && !strcmp(v, "true")) skip = 1;
				else if (!strcmp(line, "X-GNOME-Autostart-enabled") && !strcmp(v, "false")) skip = 1;
				else if (!strcmp(line, "OnlyShowIn") && !strstr(v, "gluewin")) skip = 1;
				else if (!strcmp(line, "NotShowIn") && strstr(v, "gluewin")) skip = 1;
				else if (!strcmp(line, "TryExec")) { char t[256]; snprintf(t, sizeof t, "%s", v); if (t[0] != '/' ? !have(t) : access(t, X_OK)) skip = 1; }
			}
			fclose(f);
			if (skip || !exec[0]) continue;
			for (char *s = exec; *s; s++) if (*s == '%' && s[1]) { s[0] = ' '; s[1] = ' '; }
			spawn(exec);
		}
		closedir(d);
	}
}

static void sigh(int s) { (void)s; quit_sig = 1; }

static void setup(void)
{
	XSetWindowAttributes swa;
	static const unsigned cs[NCUR] = {XC_left_ptr, XC_left_side, XC_right_side, XC_top_side, XC_bottom_side,
		XC_top_left_corner, XC_top_right_corner, XC_bottom_left_corner, XC_bottom_right_corner, XC_fleur};
	int rr_err;
	struct sigaction sa = {0};

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_NOCLDWAIT | SA_RESTART;
	sigaction(SIGCHLD, &sa, NULL);
	sa.sa_handler = sigh;
	sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	gfx_init();
	XSetErrorHandler(xerror_start);
	XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask |
		PropertyChangeMask | StructureNotifyMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);

	XInternAtoms(dpy, (char **)atomnames, NATOMS, False, A);
	for (int i = 0; i < NCUR; i++) cur[i] = XCreateFontCursor(dpy, cs[i]);
	XDefineCursor(dpy, root, cur[CUR_NORMAL]);
	cfg_load();
	theme_apply();
	fonts_init();
	apps_load();
	for (int i = 0; i < NWS; i++) tilews[i] = cfg.tiling;
	last_tiling = cfg.tiling;

	wmcheck = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheck, A[NetCheck], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wmcheck, 1);
	XChangeProperty(dpy, wmcheck, A[NetWMName], A[Utf8], 8, PropModeReplace, (unsigned char *)"gluewin", 7);
	XChangeProperty(dpy, root, A[NetCheck], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wmcheck, 1);
	XChangeProperty(dpy, root, A[NetSupported], XA_ATOM, 32, PropModeReplace, (unsigned char *)A, NATOMS);
	long one = 1, zero = 0;
	long nws = NWS;
	(void)one;
	XChangeProperty(dpy, root, A[NetNumDesktops], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&nws, 1);
	XChangeProperty(dpy, root, A[NetCurDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&zero, 1);
	XDeleteProperty(dpy, root, A[NetClientList]);

	if (XRRQueryExtension(dpy, &rr_event, &rr_err)) XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	(void)swa;
	int xop, xerr, xmaj = XkbMajorVersion, xmin = XkbMinorVersion;
	if (XkbQueryExtension(dpy, &xop, &xkb_event, &xerr, &xmaj, &xmin))
		XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify, XkbGroupStateMask, XkbGroupStateMask);
	kbd_refresh();
	kbd_apply();
	update_mons();
	tray_setup();
	grab_keys();
	wallpaper();

	Window r, p, *ch = NULL;
	unsigned n = 0;
	if (XQueryTree(dpy, root, &r, &p, &ch, &n)) {
		for (unsigned i = 0; i < n; i++) {
			XWindowAttributes wa;
			if (!XGetWindowAttributes(dpy, ch[i], &wa) || wa.override_redirect || wa.map_state != IsViewable) continue;
			if (ch[i] == wmcheck || ch[i] == trayselwin || monbar(ch[i]) >= 0) continue;
			manage(ch[i], &wa);
		}
		if (ch) XFree(ch);
	}
	focus_top();
	if (!getenv("GLUEWIN_NO_AUTOSTART")) autostart();
	if (fork() == 0) {
		signal(SIGCHLD, SIG_DFL);
		close(ConnectionNumber(dpy));
		if (nice(15) < 0) {}
		icon_cache_svgs();
		_exit(0);
	}
}

static void cleanup(void)
{
	close_popups();
	tray_release();
	while (clients) {
		Client *c = clients;
		if (c->minimized) XMapWindow(dpy, c->win);
		unmanage(c, 0);
	}
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, A[NetActiveWindow]);
	XSync(dpy, False);
	XCloseDisplay(dpy);
}

int main(int argc, char **argv)
{
	XEvent ev;
	int fd;
	if (argc > 1 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) { puts("gluewin 0.1"); return 0; }
	if (argc > 1 && !strcmp(argv[1], "--reload")) {
		if (!(dpy = XOpenDisplay(NULL))) return 1;
		gfx_init();
		send_reload();
		return 0;
	}
	setlocale(LC_ALL, "");
	if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "gluewin: cannot open display\n"); return 1; }
	setup();
	fd = ConnectionNumber(dpy);
	while (running && !quit_sig) {
		while (XPending(dpy) && running) { XNextEvent(dpy, &ev); handle(&ev); }
		if (!running || quit_sig) break;
		fd_set fds;
		struct timeval tv;
		time_t t = time(NULL);
		int to = 60 - (int)(t % 60);
		tv.tv_sec = MIN(to, 15);
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if (select(fd + 1, &fds, NULL, NULL, &tv) == 0) tb_draw_all();
		XFlush(dpy);
	}
	cleanup();
	return 0;
}
