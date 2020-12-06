/* appearance */
static const int sloppyfocus        = 1;  /* focus follows mouse */
static const unsigned int borderpx  = 2;  /* border pixel of windows */
static const float rootcolor[]      = {0.3, 0.3, 0.3, 1.0};
static const float bordercolor[]    = {0.1960784, 0.1882352, 0.1882352, 1.0};
static const float focuscolor[]     = {0.9137254, 0.3294117, 0.1254901, 1.0};

/* gaps */
static const unsigned int gappih    = 15; /* horiz inner gap between windows */
static const unsigned int gappiv    = 15; /* vert inner gap between windows */
static const unsigned int gappoh    = 15; /* horiz outer gap between windows and screen edge */
static const unsigned int gappov    = 15; /* vert outer gap between windows and screen edge */
static const int smartgaps          = 1;  /* 1 means no outer gap when there is only one window */

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static const Rule rules[] = {
	/* app_id     title       tags mask   isfloating    monitor x    y   width height */
	/* examples:
	{ "Gimp",     NULL,       0,            1,           -1, 	0, 	 0,   500, 400 },
	{ "firefox",  NULL,       1 << 8,       0,           -1, 	200, 100, 0,   0 },
	*/
	/* x, y, width, heigh are floating only
	* When x or y == 0 the client is placed at the center of the screen,
	* when width or height == 0 the default size of the client is used*/

	{ "gnome-calculator", NULL, 0, 1, -1, 			0, 0, 200, 400 },
	{ "fzfLauncher.sh", NULL, 0, 1, -1, 			0, 0, 0, 0 },
	{ "wayPreviewServer", NULL, 0, 1, -1, 			500, 50, 0, 0 },
	{ "Alacritty", NULL, 0, 0, -1, 					0, 0, 0, 0 },
	{ "firefoxdeveloperedition", NULL, 0, 0, -1, 	0, 0, 0, 0 },
	{ "firefox", 				 NULL, 0, 0, -1, 	0, 0, 0, 0 },
};

/* layout(s) */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* monitors */
static const MonitorRule monrules[] = {
	/* name       mfact nmaster scale layout       rotate/reflect */
	/* example of a HiDPI laptop monitor:
	{ "eDP-1",    0.5,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL },
	*/
	/* defaults */
	{ "eDP-1",		0.55, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL },
	{ "HDMI-A-1",	0.55, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL },
	{ "HDMI-A-2",	0.55, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL },
	{ "DP-1",		0.55, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL },
	/* with the outputOder patch, the order in which every monitor is defined
	 * defines its actual position. Non configured monitor, are always added to
	 * the left */
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
    .options = "caps:swapescape",
    .layout = "gb"
};

/* Trackpad */
int tap_to_click = 1;
int natural_scrolling = 0;

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* gb will be set the first time togglekblayout is called, then us.. it is
 * recommended to set the same layout in position 0 of kblayouts and in
 * xkb_rules */
static const char *kblayouts[] = {"gb", "it"};

#define MODKEY WLR_MODIFIER_LOGO
#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1 << TAG} }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static const char *termcmd[]  = { "alacritty", NULL };
static const char *browsercmd[] = { "firefox", NULL };
static const char *menucmd[] = { "fzfLauncherWrap.sh", NULL };
static const char *guifmcmd[] = { "thunar", NULL };
static const char *calcmd[] = { "gnome-calculator", NULL };
static const char *idecmd[] = { "qtcreator.sh", NULL };
static const char *vUpcmd[] = { "volume.sh", "up", "5", NULL };
static const char *vDowncmd[] = { "volume.sh", "down", "5", NULL };
static const char *vMutecmd[] = { "volume.sh", "audiomute", NULL };
static const char *micMutecmd[] = { "volume.sh", "micmute", NULL };
static const char *brightUcmd[] = { "light.sh", "u", NULL };
static const char *brightDcmd[] = { "light.sh", "d", NULL };
static const char *colorPickerCmd[] = { "colorPicker.sh", NULL };
static const char *shotAreaCmd[] = { "shot.sh", "area", NULL };
static const char *shotAllCmd[] = { "shot.sh", "all", NULL };
/* static const char *monitorCmd[] = { "monitor.sh", NULL }; */
static const char *monitorRestoreCmd[] = { "monitor.sh", "r", NULL };
static const char *oneMonitorCmd[] = { "monitor.sh", "1", NULL };
static const char *tvOnlyCmd[] = { "monitor.sh", "2", NULL };
static const char *allMonitorsCmd[] = { "monitor.sh", "3", NULL };
static const char *rotateCmd[] = { "rotate.sh", NULL };
static const char *suspendCmd[] = { "systemctl", "suspend", NULL };

#include "shiftview.c"

static const Key keys[] = {
	/* Note that Shift changes certain key codes: c -> C, 2 -> at, etc. */
	/* modifier                  	key                 function        	argument */
	{ MODKEY,                    	XKB_KEY_Return, 	spawn,          	{.v = termcmd} },
	{ MODKEY,                    	XKB_KEY_b,         	spawn,          	{.v = browsercmd} },
	{ MODKEY,					 	XKB_KEY_p,         	spawn,          	{.v = menucmd} },
	{ 0, XKB_KEY_XF86Calculator,      					spawn, 		 		{.v = calcmd} },
	{ 0, XKB_KEY_XF86Explorer,        					spawn, 		 		{.v = guifmcmd} },
	{ 0, XKB_KEY_XF86Tools,           					spawn, 		 		{.v = idecmd} },
	{ 0, XKB_KEY_XF86AudioRaiseVolume, 					spawn, 		 		{.v = vUpcmd} },
	{ 0, XKB_KEY_XF86AudioLowerVolume, 					spawn, 		 		{.v = vDowncmd} },
	{ 0, XKB_KEY_XF86AudioMute,       					spawn, 		 		{.v = vMutecmd} },
	{ 0, XKB_KEY_XF86AudioMicMute,       				spawn, 		 		{.v = micMutecmd} },
	{ 0, XKB_KEY_XF86MonBrightnessUp,       			spawn, 		 		{.v = brightUcmd} },
	{ 0, XKB_KEY_XF86MonBrightnessDown,       			spawn, 		 		{.v = brightDcmd} },
	{ MODKEY, 						XKB_KEY_Print,     	spawn, 		 		{.v = colorPickerCmd} },
	{ WLR_MODIFIER_SHIFT, 		 	XKB_KEY_Print,     	spawn, 		 		{.v = shotAreaCmd} },
	{ 0, 						 	XKB_KEY_Print,     	shotFocusMon, 	 	{0} },
	{ WLR_MODIFIER_CTRL, 		 	XKB_KEY_Print,     	spawn, 		 		{.v = shotAllCmd} },
	{ MODKEY, 						XKB_KEY_o,     		spawn, 		 		{.v = monitorRestoreCmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT, 	XKB_KEY_O,     		spawn, 		 		{.v = oneMonitorCmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT|WLR_MODIFIER_CTRL, XKB_KEY_O, spawn, 		{.v = tvOnlyCmd} },
	{ MODKEY|WLR_MODIFIER_CTRL, 	XKB_KEY_o,     		spawn, 		 		{.v = allMonitorsCmd} },
	{ MODKEY, 						XKB_KEY_r,     		spawn, 		 		{.v = rotateCmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT, 	XKB_KEY_S,     		spawn, 		 		{.v = suspendCmd} },
	{ MODKEY,                    	XKB_KEY_j,         	focusstack,     	{.i = +1} },
	{ MODKEY,                    	XKB_KEY_k,         	focusstack,     	{.i = -1} },
	{ MODKEY,                    	XKB_KEY_u,         	incnmaster,     	{.i = +1} },
	{ MODKEY,                    	XKB_KEY_d,         	incnmaster,     	{.i = -1} },
	{ MODKEY,                    	XKB_KEY_h,         	setmfact,       	{.f = -0.05} },
	{ MODKEY,                    	XKB_KEY_l,         	setmfact,       	{.f = +0.05} },
	{ MODKEY,                    	XKB_KEY_BackSpace, 	zoom,           	{0} },
	{ MODKEY,                    	XKB_KEY_Tab,       	view,           	{0} },
	{ WLR_MODIFIER_ALT,          	XKB_KEY_j,         	shiftview,      	{ .i = -1 } },
	{ WLR_MODIFIER_ALT,          	XKB_KEY_k,  		shiftview,      	{ .i = 1 } },
	{ MODKEY,					 	XKB_KEY_q,         	killclient,     	{0} },
	{ MODKEY,                    	XKB_KEY_t,         	setlayout,      	{.v = &layouts[0]} },
	{ MODKEY,                    	XKB_KEY_f,         	setlayout,      	{.v = &layouts[1]} },
	{ MODKEY,                    	XKB_KEY_m,         	setlayout,      	{.v = &layouts[2]} },
	{ MODKEY,                    	XKB_KEY_w,     		setlayout,      	{0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, 	XKB_KEY_space,     	togglefloating, 	{0} },
	{ MODKEY, 					 	XKB_KEY_e,    		togglefullscreen, 	{0} },
	{ MODKEY,                    	XKB_KEY_0,         	view,           	{.ui = ~0} },
	{ MODKEY,                    	XKB_KEY_s,         	tag,            	{.ui = ~0} },
	{ MODKEY,                    	XKB_KEY_comma,     	focusmon,       	{.i = -1} },
	{ MODKEY,                    	XKB_KEY_period,    	focusmon,       	{.i = +1} },
	{ MODKEY,                    	XKB_KEY_plus,      	tagmon,         	{.i = -1} },
	{ MODKEY,                    	XKB_KEY_minus,     	tagmon,         	{.i = +1} },
	{ MODKEY,  					 	XKB_KEY_space,    	togglekblayout, 	{0} },
	TAGKEYS(          XKB_KEY_1, 	XKB_KEY_exclam,     0),
	TAGKEYS(          XKB_KEY_2, 	XKB_KEY_quotedbl,   1),
	TAGKEYS(          XKB_KEY_3, 	XKB_KEY_sterling,   2),
	TAGKEYS(          XKB_KEY_4, 	XKB_KEY_dollar,     3),
	TAGKEYS(          XKB_KEY_5, 	XKB_KEY_percent,    4),
	TAGKEYS(          XKB_KEY_6, 	XKB_KEY_caret,      5),
	TAGKEYS(          XKB_KEY_7, 	XKB_KEY_ampersand,  6),
	TAGKEYS(          XKB_KEY_8, 	XKB_KEY_asterisk,   7),
	TAGKEYS(          XKB_KEY_9, 	XKB_KEY_parenleft,  8),
	{ MODKEY|WLR_MODIFIER_SHIFT, 	XKB_KEY_Q,          quit,          	{0} },
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
};
