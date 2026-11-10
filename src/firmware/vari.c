/* SP-404SX/A EXTRAfirmware: per-pad tape-style varispeed for the Roland SP-404SX / SP-404A (system 1.07).
   - TIME/BPM page, CTRL 3: per-pad speed (fully left OFF, -12 .. +7 semitones, fully right PTN = follow the
     pattern tempo, 0.25x .. 4x). The display shows the tempo you hear; CTRL 1 / CTRL 2 show their value x speed.
   - Audio: a wrapper around every sample feeder's fill() asks Roland's fill for floor(phase + F*ratio) source
     frames and resamples them (linear interpolation) in place into the F-frame block. Normal speed calls
     Roland's fill directly (bit-identical to stock).
   - Saving: speeds are kept in VARISPD.BIN on the card and follow the sample (copy / exchange / delete).
   - Chromatic mode, per bank: SHIFT + TIME/BPM opens a chromatic menu (TIME/BPM blinks) for the last pad played
     and makes it its bank's source: the bank's 12 pads play it at 0..+11 semitones + 12 x octave, MIDI notes on
     the bank's own channel play it with note 60 = its own pitch (silent above B6). Menu: CTRL 1 OFF / MIDI channel
     (free ones only), CTRL 2 MON (poly to come), CTRL 3 octave -2..+2. Any number of banks at once. Keys work on the play
     page, the TIME/BPM page and while resampling (RDY / REC); elsewhere the pads play as stock. Saved in
     CHROMA.BIN; a bank turns off when its source sample changes.
   Placement (see link.ld and src/tools/build.py): the free 1 KB above the vector table, Roland's hidden PAD_INFO
   debug dump, two unreferenced Roland routines, and methods of the disabled factory test pages.
   Every Roland function called from here is named by its address in the official 1.07 image. */

typedef signed char s8;
typedef short s16;
typedef unsigned short u16;
typedef unsigned int u32;

#ifndef R_GET
#define R_GET    ((int  (*)(int id, int pad))0x0C082B34)
#define R_DIV    ((int  (*)(int a, int b))0x0C0F48AC)
#define R_SHOWN  ((void (*)(int n))0x0C0CFFF8)
#define R_SHOWS  ((void (*)(const char *s, int f))0x0C0CFF84)
#endif
#ifndef SECT
#define SECT(x) __attribute__((section(x)))
#endif

#define P_CURPAD 52
#define P_ORIG   42
#define P_SYNC   43
#define P_MOD    44
#define MODE_VARI 8
#define MAXV     12   /* SP-404SX polyphony; a voice numbered above this just plays at normal speed */
#define BTN_BPM  40   /* TIME/BPM button: chromatic gesture; the only button whose stock handling differs between modes 7 and 8 */
#define UNITY    0x10000u

struct page  { void *vt; int mode; int f8; int f12; };   /* f12 == 1: CTRL 2 shows the pattern tempo */
struct event { int type; unsigned char id; s8 val; };

extern const unsigned char vari_ratio_u8[128];

/* Persistent per-pad state, saved verbatim as VARISPD.BIN:
   "VSP1", then per pad: speed (knob position - 64, 0 = normal) and a fingerprint of the pad's sample. */
struct vtab { u32 magic; struct { s8 pos; unsigned char fp; } e[120]; };
struct vtab vari_tab SECT(".vbss");
volatile int vari_dirty_ms SECT(".vbss");      /* >0: unsaved change; counts ms since the last change */
/* Set by the PAD_INFO load hook; the next tick reads our file. Starts at 1 (stored in the image, not zeroed RAM)
   so the very first tick loads the file before anything could be written. */
volatile int vari_need_load __attribute__((section(".cavedata"))) = 1;
#define vari_pos_of(pad) (vari_tab.e[pad].pos)
#ifndef CAVE
#define CAVE __attribute__((section(".text.cave")))
#endif

struct vstate { s16 aL, aR, bL, bR; u32 ph; int nxt; };   /* nxt = expected cursor + 1; 0 = not running (RAM starts zeroed) */
struct vstate vari_vs[MAXV] SECT(".vbss");

/* ------------------------------------------------------------------ chromatic mode (state) */
#ifndef CHR
#define CHR(n) __attribute__((noinline, section(".text.chr" #n)))   /* reclaimed factory test page methods (link.ld) */
#endif
/* Settings per bank A..J, saved verbatim as CHROMA.BIN: "CHR1", then per bank: source pad + 1 (0 = off), MIDI
   channel 0..15, mode (bits 0..2 octave + 2, bit 3 poly) and a fingerprint of the source's sample. The source is
   one of the bank's own pads; while it is set, the bank's 12 pads are keys: pad 1 = the source's own pitch,
   + 1 semitone per pad, + 12 x octave. MIDI notes on the bank's channel play it too: note 60 = own pitch. */
struct cbank { unsigned char src, ch, mode, fp; };
struct ctab { u32 magic; struct cbank b[10]; };
struct ctab chr_tab SECT(".chrdata");
#define CHR_MAGIC 0x43485231u     /* "CHR1" */
/* Running state (not saved): per bank the key pad + 1 / MIDI note + 1 that sounds (for releases) and the
   pitch the source's voice plays (semitones + 64, 0 = its own pitch); the pitch of the start in progress; the
   menu (bank, source pad, last knob, open request); C.ON redraw request; unsaved-change timer; display text. */
struct chr { unsigned char key[10], mkey[10], cur[10], pend, mb, mpad, mk, want, show, menu; volatile int dirty; char txt[4]; };
struct chr chr_st SECT(".chrdata");
#define S0       64               /* semitone offset in pend / cur */
#define S_MAX    35               /* highest semitone (just under 8x) */
#define MODE_CHR 9                /* TIME/BPM page: chromatic menu */
#define BTN_SHIFT 20
#define BTN_CANCEL 34
/* 2^(k/12) - 1 in Q16 for k = 0..11 */
static const u16 chr_semi[12] SECT(".rodata.chr") = { 0, 3897, 8026, 12400, 17034, 21944, 27146, 32657, 38496, 44682, 51236, 58179 };
static const char chr_on_s[] SECT(".rodata.chr") = "C.ON";
static const char chr_mon_s[] SECT(".rodata.chr") = "MON";
static int chr_menu(struct page *p);
static int chr_padkey(struct event *e);
static unsigned char pad_fp(int pad);

/* ------------------------------------------------------------------ mute groups (state) */
#ifndef MUTE
#define MUTE(n) __attribute__((noinline, section(".text.mute" #n)))   /* reclaimed factory test page methods (link.ld) */
#endif
/* "MUT1" and per pad its mute group (0 = none, 1..10): saved verbatim as the start of MUTEGRP.BIN; the SHIFT + MARK
   menu: open, the pad it edits, a MARK release to take, display text; unsaved-change timer. */
struct mute { u32 magic; unsigned char g[120], open, pad, eat; char txt[4]; volatile int dirty; };
struct mute mute_st SECT(".mutedata");
static void mute_choke(int pad);

/* ------------------------------------------------------------------ pan (state) */
#ifndef PAN
#define PAN(n) __attribute__((noinline, section(".text.pan" #n)))      /* reclaimed factory test page methods (link.ld) */
#endif
/* "PAN1" and per pad its pan, -50 (L50) .. 0 (centre) .. +50 (R50): saved verbatim as the start of PAN.BIN, the same
   shape as mute_st's start; unsaved-change timer. */
struct pan { u32 magic; s8 v[120]; volatile int dirty; };
struct pan pan_tab SECT(".pandata");
#define MODE_PAN 10               /* TIME/BPM page: pan menu */
#define BTN_SEL  39               /* START/END/LEVEL button */
#define LED_BPM  21               /* TIME/BPM LED (Roland's button -> LED table 0x0C138B74: button 40) */
#define LED_SEL  22               /* START/END/LEVEL LED (button 39) */
static void pan_open(struct page *p);
static void pan_show(void);

/* ------------------------------------------------------------------ speed + UI */
#ifndef R_TEMPO
#define R_TEMPO  ((int  (*)(void))0x0C0BAF24)   /* pattern tempo x100 (102.00 BPM = 10200); what stock PTN uses */
#endif
#ifndef MIDI
#define MIDI __attribute__((section(".text.midi")))   /* reclaimed CPageTestMidi method */
#define LED  __attribute__((section(".text.led")))    /* reclaimed CPageTestLED method  */
#endif
#ifndef DEV
#define DEV __attribute__((section(".text.dev")))
#endif
#define P_NOTM   40                 /* 1: Time Modify not available on this pad (stock shows "---") */
#define PTN_POS  (125 - 64)         /* knob 125..127: PTN, follow the pattern tempo */
#define STR_DASH ((const char *)0x0C12F3E8)    /* Roland's "---" */
#define STR_OFF  ((const char *)0x0C12F3EC)    /* Roland's "OFF" */
#define STR_PTN  ((const char *)0x0C12F3F0)    /* Roland's "PTN" */
#define R_MIN    0x4000u            /* PTN range: 0.25x (-24 semitones) */
#define R_MAX    0x3FFFFu           /* .. just under 4x (+24 st) */
#define V_MAX    0x7FFFFu           /* the resampler supports any ratio < 8 (chromatic notes up to +35 st) */


/* Stock Time Modify rate in 1/1000 (0x0C09611C): target * 1000 / orig, folded by octaves into 500..1300. */
LED __attribute__((noinline)) static int tm_rate(int target10, int orig10) {
    int r;
    if (target10 <= 0 || target10 > 30000) return 1000;            /* not a real tempo: no stretch */
    r = R_DIV(target10 * 1000, orig10);
    while (r > 1300) r /= 2;
    while (r > 0 && r < 500) r *= 2;
    return r;
}
/* Tempo x10 the pad plays at with Time Modify applied (before varispeed), as stock playback computes it. */
DEV __attribute__((noinline)) static int base_bpm(int pad) {
    int orig = R_GET(P_ORIG, pad);
    if (orig > 0 && orig <= 30000 && R_GET(P_NOTM, pad) != 1) {
        int sync = R_GET(P_SYNC, pad);
        if (sync == 2) return R_DIV(orig * tm_rate(R_GET(P_MOD, pad), orig), 1000);
        if (sync == 1) return R_DIV(orig * tm_rate(R_DIV(R_TEMPO(), 10), orig), 1000);
    }
    return orig;
}
DEV static int cur_pad(void) {
    int pad = R_GET(P_CURPAD, -1);
    return (pad >= 0 && pad < 120) ? pad : -1;
}
/* Speed ratio for a pad in Q16 (0x10000 = normal). Any stored byte gives a ratio in [0.25, 4). */
DEV __attribute__((noinline)) u32 pad_ratio(int pad) {
    int pos = vari_pos_of(pad);
    if (pos < PTN_POS) return ((u32)vari_ratio_u8[(pos + 64) & 127] + 128) << 8;
    if (R_GET(P_NOTM, pad) != 1 && R_GET(P_SYNC, pad) == 1) return UNITY;   /* Time Modify already follows */
    int base = base_bpm(pad), t = R_TEMPO();
    if (base <= 0 || base > 30000 || t <= 0 || t > 30000) return UNITY;
    u32 r = (u32)R_DIV(t << 16, base * 10);
    if (r < R_MIN) r = R_MIN;
    if (r > R_MAX) r = R_MAX;
    return r;
}
DEV __attribute__((noinline)) static void show_bpm(int x10, u32 r) {
    R_SHOWN(R_DIV((int)(((u32)x10 * r) >> 16) + 5, 10));
}
__attribute__((noinline, section(".text.vshow"))) static void vari_show(void) {
    int pad = cur_pad();
    const char *txt = STR_DASH;
    if (pad >= 0) {
        int pos = vari_pos_of(pad);
        txt = STR_OFF;
        if (pos >= PTN_POS) txt = STR_PTN;
        else if (pos > 2 - 64) { show_bpm(base_bpm(pad), pad_ratio(pad)); return; }
    }
    R_SHOWS(txt, 1);
}
/* Display hook. Mode 8: CTRL 3. Modes 6 (CTRL 2) and 7 (CTRL 1): when the pad has a speed, show the
   tempo you hear (the stock value x speed); otherwise, and for stock texts, Roland's display runs. */
CAVE int vari_display(struct page *p) {
    int m = p->mode, pad, v;
    u32 r;
    if (chr_menu(p)) return 1;
    if (m == MODE_VARI) { vari_show(); return 1; }
    if (m != 6 && m != 7) return 0;
    pad = cur_pad();
    if (pad < 0) return 0;
    r = pad_ratio(pad);
    if (r == UNITY) return 0;
    if (m == 6) {
        if (p->f12 == 1) return 0;            /* stock shows the pattern tempo here */
        v = R_GET(P_ORIG, pad);
    } else {
        if (R_GET(P_NOTM, pad) == 1 || R_GET(P_SYNC, pad) != 2) return 0;   /* "---", OFF, PTN */
        v = R_GET(P_MOD, pad);
    }
    show_bpm(v, r);
    return 1;
}
#ifndef KNOBPOS
#define KNOBPOS  ((volatile const s16 *)0x0CDAA428)   /* Roland's current knob positions (0..127), by knob id */
#endif
s16 vari_snap[2] SECT(".vbss");        /* CTRL 1 / CTRL 2 positions when CTRL 3 was last turned */

/* CTRL 3 turned: store the speed, remember where CTRL 1 / CTRL 2 are, show the CTRL 3 view. */
LED __attribute__((noinline)) static void ctrl3(struct page *p, int v, int pad) {
    if (pad >= 0) {
        if (v < 0) v = 0;
        if (vari_pos_of(pad) != (s8)(v - 64)) { vari_pos_of(pad) = (s8)(v - 64); vari_dirty_ms = 1; }
    }
    vari_snap[0] = KNOBPOS[0];
    vari_snap[1] = KNOBPOS[1];
    p->mode = MODE_VARI;
    vari_show();
}
/* Event hook (all events of the TIME/BPM page).
   CTRL 3: our speed; the page shows the CTRL 3 view (mode 8) until CTRL 1 / CTRL 2 is really turned or the
   TIME/BPM button is pressed. Every other event is passed to Roland with mode 8 left alone: Roland's handlers
   behave the same for modes 7 and 8 except for CTRL 1 / CTRL 2 and that button, which get mode 7 first.
   CTRL 1 / CTRL 2 moving by 2 steps or less from where they were while CTRL 3 was turned counts as knob noise.
   CTRL 2 (original BPM) also resets the speed, as Roland's CTRL 2 resets Time Modify. */
MIDI int vari_event(struct page *p, struct event *e) {
    int m = p->mode;
    if (e->type == 1 && m >= 5 && m <= MODE_VARI) {
        int pad = cur_pad(), id = e->id;
        if (id == 2) { ctrl3(p, e->val, pad); return 1; }
        if (id <= 1) {
            if (m == MODE_VARI) {
                int d = e->val - vari_snap[id];
                if (d >= -2 && d <= 2) return 1;              /* noise: keep showing CTRL 3 */
                p->mode = 7;
            }
            if (id == 1 && pad >= 0 && p->f12 == 0 && vari_pos_of(pad) != 0) {
                vari_pos_of(pad) = 0;
                vari_dirty_ms = 1;
            }
        }
        return 0;
    }
    if (m == MODE_VARI && e->type == 0 && e->id == BTN_BPM) p->mode = 7;
    return 0;
}

/* ------------------------------------------------------------------ chromatic mode */
#ifndef R_PLAYPAD
#define R_PLAYREQ ((int  (*)(void *q, int kind, int pad, int vel))0x0C093D28) /* play request: drops pads with no
                                                                         sample loaded, then starts the pad      */
#define R_PLAYPAD ((int  (*)(int kind, int pad, int vel))0x0C084974)  /* start a pad (stops its old voice first) */
#define R_PLAYING ((int  (*)(int pad))0x0C0842CC)                     /* 1 while the pad is playing              */
#define R_STOPPAD ((void (*)(int pad, int how))0x0C084400)            /* stop the pad's voices                   */
#define R_VFREE   ((void (*)(int n))0x0C0845AC)                       /* voice list upkeep after a stop (with    */
#define R_VFREE2  ((void (*)(void))0x0C0845B8)                        /* R_VFREE2), as play_pad does             */
#define R_RELPAD  ((void (*)(int pad))0x0C084B48)                     /* pad released (gate: stop it)            */
#define R_HELD    ((int  (*)(int id))0x0C0BEA7C)                      /* 1 while panel button `id` is held       */
#define R_MSG     ((void (*)(const char *s))0x0C0D0150)               /* play page text, as Roland's P.ON / P.OF  */
#define CURPAGE   (*(void *const volatile *)0x0CDAF934)               /* page manager: page shown now             */
#define FWDMODE   (*(volatile const int *)0x0C138F5C)                 /* Roland: SHIFT + TIME/BPM = FWD while set */
#define REQOBJ    ((void *)0x0CCAE0C4)                                /* the note handler's play request object   */
#define R_SMPKICK ((int  (*)(void))0x0C08FDDC)                        /* sampler armed (resample RDY): start it   */
#define R_SETPAGE ((void (*)(void *mgr, int slot))0x0C0BE884)         /* show page `slot` (1 = play page)         */
#define HOLDST    ((volatile int *)0x0CE327DC)                        /* HOLD state per pressed pad, 3 = latched    */
#define R_PMASK   ((unsigned (*)(int bank))0x0C085074)                /* bit n: pad n of the bank has a voice playing */
#endif
#define VT_PLAY  0x0C12E044u   /* CPagePlay vtable     */
#define VT_EDIT  0x0C12F3F8u   /* CPageSmplEdit (TIME/BPM page) vtable */
#define VT_RESMP 0x0C12F270u   /* CPageReSmpling vtable; page +8 = state: 1..2 choosing the destination pad
                                  (pads select it), 3 / 4 RDY / 5 REC (pads ignored by the page) */
#define P_BANK   53            /* current bank 0..9: panel pad n of the bank is pad bank * 12 + n */
#define P_MIDICH 2             /* the SP-404SX's MIDI channel (< 0: off); pads 60..119 use the next channel */
#define K_MIDI   3             /* note event sources that are someone playing: MIDI in, panel pads */
#define K_PANEL  6

/* The page shown now: 1 = play page or TIME/BPM page, 2 = resampling with the destination chosen (RDY / REC),
   3 = resampling while choosing the destination, 0 = anything else (pattern mode and pattern recording have
   their own pages, so they are 0). Chromatic keys play on 1 and 2. */
CHR(page) static int chr_page(void) {
    const u32 *pg = CURPAGE;
    u32 vt;
    if (!pg) return 0;
    vt = pg[0];
    if (vt == VT_PLAY || vt == VT_EDIT) return 1;
    if (vt == VT_RESMP) return (int)pg[2] >= 3 ? 2 : 3;
    return 0;
}
#define chr_ok() ((unsigned)(chr_page() - 1) < 2u)

/* Bank of a pad 0..119 (no division: not on the allow-list). */
CHR(bankof) static int chr_bankof(int pad) {
    int b = 0;
    while (pad >= 12) { pad -= 12; b++; }
    return b;
}
/* Bank whose chromatic keys include this pad, while keys are usable, else -1. */
CHR(keyof) static int chr_keyof(int pad) {
    int b;
    if (pad < 0 || pad >= 120 || !chr_ok()) return -1;
    b = chr_bankof(pad);
    return chr_tab.b[b].src ? b : -1;
}
/* Bank this pad is the chromatic source of, else -1. */
CHR(srcbank) static int chr_srcbank(int pad) {
    int b;
    if (pad < 0 || pad >= 120) return -1;
    b = chr_bankof(pad);
    return chr_tab.b[b].src == pad + 1 ? b : -1;
}
/* Channels taken: the SP-404SX's own two and the other chromatic banks'. */
CHR(used) static int chr_used(int ch, int self) {
    int b, own = R_GET(P_MIDICH, -1);
    if (own >= 0 && (ch == own || ch == own + 1)) return 1;
    for (b = 0; b < 10; b++)
        if (b != self && chr_tab.b[b].src && chr_tab.b[b].ch == ch) return 1;
    return 0;
}
/* The n-th free channel (n >= 0, -1 if none), or for n < 0 the number of free channels. */
CHR(free) static int chr_free(int self, int n) {
    int ch, k = 0;
    for (ch = 0; ch < 16; ch++)
        if (!chr_used(ch, self)) { if (k == n) return ch; k++; }
    return n < 0 ? k : -1;
}

/* ---- the chromatic menu: mode 9 of the TIME/BPM page. SHIFT + TIME/BPM opens it for the current pad (the last
   pad played) and turns that pad's bank chromatic with it as the source (lowest free MIDI channel, mono, octave
   0) unless it already is. CTRL 1: OFF / free MIDI channels, CTRL 2: MON (poly to come), CTRL 3: octave -2..+2.
   The TIME/BPM button blinks (LED patch) and leaves the menu (Roland leaves the page in modes other than 5..7). */
CHR(open) static void chr_open(struct page *p) {
    int pad = R_GET(P_CURPAD, -1), b, ch;
    chr_st.want = 0;
    if (pad < 0 || pad >= 120) return;
    b = chr_bankof(pad);
    if (chr_tab.b[b].src != pad + 1) {
        ch = chr_free(b, 0);
        if (ch < 0) return;
        chr_tab.b[b].src = (unsigned char)(pad + 1);
        chr_tab.b[b].ch = (unsigned char)ch;
        chr_tab.b[b].mode = 2;                  /* octave 0, mono */
        chr_tab.b[b].fp = pad_fp(pad);
        chr_st.cur[b] = 0;
        chr_st.dirty = 1;
    }
    chr_st.mb = (unsigned char)b;
    chr_st.mpad = (unsigned char)pad;
    chr_st.mk = 0;
    chr_st.menu = 1;
    p->mode = MODE_CHR;
}
/* Bank off (CTRL 1 fully left, CANCEL): release the note sounding first; HOLD may have latched it on a key, whose
   release no longer reaches the source once the bank is off. */
CHR(off) static void chr_off(struct cbank *c) {
    int s = c->src;
    c->src = 0;
    if (s) R_RELPAD(s - 1);
}
CHR(knob) static void chr_knob(int id, int v) {
    struct cbank *c = &chr_tab.b[chr_st.mb];
    u32 old = *(u32 *)c;
    if (v < 0) v = 0;
    if (id == 0) {                                  /* OFF fully left, then the free channels */
        int i = (v * (chr_free(chr_st.mb, -1) + 1)) >> 7;
        if (!i) chr_off(c);
        else {
            c->ch = (unsigned char)chr_free(chr_st.mb, i - 1);
            c->src = (unsigned char)(chr_st.mpad + 1);
            c->fp = pad_fp(chr_st.mpad);
        }
    } else if (id == 1) c->mode &= 7;                 /* mono only for now (poly: later); bit 3 stays clear */
    else c->mode = (unsigned char)((c->mode & 8) | ((v * 5) >> 7));                   /* 5 zones: -2..+2 */
    chr_st.mk = (unsigned char)id;
    if (*(u32 *)c != old) chr_st.dirty = 1;
}
static char *show_num(int c, int n);
CHR(show) static void chr_show(void) {
    const struct cbank *c = &chr_tab.b[chr_st.mb];
    const char *s;
    char *t;
    int n;
    if (chr_st.mk == 1) s = chr_mon_s;
    else if (chr_st.mk == 2) {                      /* o-2 .. o 2 */
        n = (c->mode & 7) - 2;
        s = t = show_num('o', n < 0 ? -n : n);
        if (n < 0) t[1] = '-';
    } else if (!c->src) s = STR_OFF;
    else s = show_num('C', c->ch + 1);              /* C 1 .. C16 */
    R_SHOWS(s, 1);
}
/* Display hook of the TIME/BPM page: open the menu if SHIFT + TIME/BPM asked for it, then draw it. A mode 9 left
   over from an earlier visit (menu closed) falls back to the CTRL 1 view. */
CHR(menu) static int chr_menu(struct page *p) {
    if (chr_st.want) { if (chr_st.want == 2) pan_open(p); else chr_open(p); }
    if (p->mode == MODE_PAN) {                      /* the pan menu; a mode 10 left over: START/END/LEVEL view */
        if (chr_st.menu != 2) { p->mode = 1; return 0; }
        pan_show();
        return 1;
    }
    if (p->mode != MODE_CHR) return 0;
    if (chr_st.menu != 1) { p->mode = 7; return 0; }
    chr_show();
    return 1;
}
/* Hold SHIFT + TIME/BPM (the menu opens), then press CANCEL: the bank turns off as with CTRL 1 fully left, and the
   menu closes for the main screen, as TIME/BPM does (issue #2). Any other CANCEL goes to Roland. */
CHR(cancel) static int chr_cancel(struct event *e) {
    if (!e->val || !R_HELD(BTN_SHIFT) || !R_HELD(BTN_BPM)) return 0;
    chr_knob(0, 0);
    chr_st.menu = 0;
    R_SETPAGE(0, 1);
    return 1;
}
/* Events in the menu: knobs set the bank; TIME/BPM leaves for the main screen (SHIFT + TIME/BPM is caught by
   vari_ev first and turns the bank on again; it only gets here in FWD mode, where it leaves too). Pads: while the
   bank is chromatic they only play (the page stays); with CTRL 1 at OFF they go to Roland, which leaves the page
   for another pad, as on the TIME/BPM page itself. */
CHR(menuev) static int chr_menu_ev(struct event *e) {
    if (e->type == 1 && e->id <= 2) { chr_knob(e->id, e->val); chr_show(); return 1; }
    if (e->type != 0) return 0;
    if (e->id >= 1 && e->id <= 12) return chr_tab.b[chr_st.mb].src != 0;
    if (e->id == BTN_BPM && e->val) { chr_st.menu = 0; R_SETPAGE(0, 1); return 1; }
    return 0;
}
/* TIME/BPM page outside the menu (TIME/BPM and START/END/LEVEL views): a pad of a chromatic bank is a key, so
   pressing it keeps the page (Roland would leave it for another pad). */
CHR(padkey) static int chr_padkey(struct event *e) {
    int id = e->id;
    return e->type == 0 && e->val && id >= 1 && id <= 12 && chr_keyof(R_GET(P_BANK, -1) * 12 + id - 1) >= 0;
}
/* HOLD (replaces Roland's pointers to R_GET in its hold check 0x0C0F1D50 and its release-all 0x0C0F1E94, which ask
   whether a pad has GATE on): a key of a chromatic bank answers with its source's setting, so HOLD latches every key
   and release-all keeps it. */
typedef int (*getfn)(int id, int pad);
CHR(padkey) int chr_holdget(int id, int pad, getfn orig) {
    int b = chr_keyof(pad);
    if (b >= 0) pad = chr_tab.b[b].src - 1;
    return orig(id, pad);
}
/* Play page event hook (after the mute group menu's, mute_page_event): SHIFT + TIME/BPM / SHIFT + START/END/LEVEL ask
   the TIME/BPM page that Roland is about to open for the chromatic (1) / pan (2) menu; any other press: neither. */
typedef int (*evfn)(void *page, struct event *e);
CHR(pageevent) int chr_page_event(void *page, struct event *e, evfn orig) {
    if (e->type == 0 && e->val)
        chr_st.want = !R_HELD(BTN_SHIFT) || FWDMODE ? 0 : e->id == BTN_BPM ? 1 : e->id == BTN_SEL ? 2 : 0;
    return orig(page, e);
}
/* Play page enter and refresh hooks (UI task): C.ON when the bank shown is chromatic, drawn on the first refresh
   after entering the page and after each chromatic note; Roland's own texts still appear in between. */
typedef int (*pgfn)(void *page);
CHR(pageenter) int chr_page_enter(void *page, pgfn orig) {
    int r = orig(page);
    chr_st.show = 1;
    chr_st.menu = 0;
    mute_st.open = 0;
    mute_st.eat = 0;                                /* a MARK release owed to the mute menu went elsewhere */
    return r;
}
CHR(pagerefres) int chr_page_refresh(void *page, pgfn orig) {
    int r = orig(page), b;
    if (chr_st.show) {
        b = R_GET(P_BANK, -1);
        if (b >= 0 && b < 10 && chr_tab.b[b].src) R_MSG(chr_on_s);
    }
    chr_st.show = 0;
    return r;
}

/* ---- playing. A key press (panel, or MIDI on the SP-404SX's own channels) requests the bank's source instead,
   before Roland's check that the pressed pad has a sample, so empty keys work. */
/* Key pad `pad` of chromatic bank b pressed: remember it, set the pitch to start, return the source pad. */
CHR(keydown) static int chr_keydown(int b, int pad) {
    chr_st.key[b] = (unsigned char)(pad + 1);
    chr_st.mkey[b] = 0;
    chr_st.pend = (unsigned char)(pad - 12 * b + 12 * ((chr_tab.b[b].mode & 7) - 2) + S0);
    chr_st.show = 1;
    return chr_tab.b[b].src - 1;
}
CHR(request) int chr_request(void *q, int kind, int pad, int vel) {
    int b = -1, r;
    if (kind == K_PANEL || kind == K_MIDI) b = chr_keyof(pad);
    if (b >= 0) pad = chr_keydown(b, pad);
    r = R_PLAYREQ(q, kind, pad, vel);
    chr_st.pend = 0;
    return r;
}
/* MIDI note handler hook: notes on a chromatic bank's channel play its source, note 60 = own pitch, silent above
   B6; note off releases it if that note is the one sounding. Everything else goes to Roland. Like Roland's handler,
   every event first lets an armed sampler start (a note starts the resample at RDY). */
typedef int (*notefn)(unsigned char *ev);
CHR(note) int chr_note(unsigned char *ev, notefn orig) {
    int b, note = ev[0], vel = ev[3], src;
    if (ev[1] != K_MIDI || note > 127) return orig(ev);
    for (b = 0; b < 10; b++)
        if (chr_tab.b[b].src && chr_tab.b[b].ch == ev[2]) break;
    if (b == 10) return orig(ev);
    R_SMPKICK();
    src = chr_tab.b[b].src - 1;
    if (vel == 0 || vel == 255) {
        if (chr_st.mkey[b] == note + 1) { chr_st.mkey[b] = 0; R_RELPAD(src); }
        return 0;
    }
    if (note - 60 > S_MAX) return 0;
    chr_st.mkey[b] = (unsigned char)(note + 1);
    chr_st.key[b] = 0;
    chr_st.pend = (unsigned char)(note - 60 + S0);
    chr_st.show = 1;
    R_PLAYREQ(REQOBJ, K_MIDI, src, vel);
    chr_st.pend = 0;
    return 0;
}
/* MIDI note gates. Roland drops every MIDI note while sampling / resampling, both where they arrive (the MIDI
   receiver's pointer to 0x0C08FD70, with the event) and again in the note handlers (their pointer to it, MIDI
   events only). While resampling with the destination chosen (RDY / REC) MIDI notes get through like pad presses:
   they play, and the first one starts the recording. Everywhere else Roland's answer stands. */
typedef int (*gatefn)(void);
CHR(gate) int chr_gate(const unsigned char *ev, gatefn orig) {
    if (ev[1] == K_MIDI && chr_page() == 2) return 1;
    return orig();
}
CHR(gate) int chr_gate2(gatefn orig) {
    return chr_page() == 2 ? 1 : orig();
}
/* Every pad start (replaces the pointer to Roland's 0x0C084974). The voice's first audio blocks are made inside
   Roland's start, so the source's pitch is set before it: the pending note, or its own pitch for any other start
   (pattern playback, other pages); Roland restarting it (MARK) keeps the current note.
   Roland stops a playing pad when it is pressed again, and for a loop pad (gate off) that press only stops it;
   another note must play, so the source is stopped here first with Roland's own stop sequence. */
CHR(stop) static void chr_stop(int pad) {
    R_STOPPAD(pad, 0);
    R_VFREE(7);
    R_VFREE2();
}
/* HOLD release-all 0x0C0F1E94 (replaces its pointer to R_PMASK): it lets go of every latched (or held) pad that has
   no voice playing. A key never has a voice of its own (the source plays it, and is all that plays in a chromatic
   bank), so while the bank plays, its pads all count as playing; the only key still latched is the one sounding
   (chr_holdclr). */
CHR(playmask) unsigned chr_playmask(int b) {
    unsigned m = R_PMASK(b);
    if (m && chr_tab.b[b].src) m = 0xFFF;
    return m;
}
/* HOLD keeps a state per pressed pad, but a chromatic bank has one voice: when a key or MIDI note takes it, keys of
   the bank latched earlier no longer sound, so their latch goes (else their next press would only be taken as their
   release). Keys held down (states 1, 2) keep theirs. */
CHR(holdclr) static void chr_holdclr(int b) {
    volatile int *h = HOLDST + 12 * b, *e = h + 12;
    do if (*h == 3) *h = 0; while (++h < e);
}
CHR(play) int chr_play(int kind, int pad, int vel) {
    int b = chr_srcbank(pad);
    if (b >= 0) {
        if (chr_st.pend) {
            chr_holdclr(b);
            if (chr_st.pend != chr_st.cur[b] && R_PLAYING(pad)) chr_stop(pad);
            chr_st.cur[b] = chr_st.pend;
        } else if (kind != K_PANEL || !chr_ok()) chr_st.cur[b] = 0;   /* pattern, other pages: its own pitch */
        /* else a panel-kind start with no key on a chromatic page: Roland restarting the source itself (e.g. MARK
           replays it from the new mark), so it keeps the note it is playing */
    }
    mute_choke(pad);
    return R_PLAYPAD(kind, pad, vel);
}
/* Every pad release (replaces the pointer to Roland's 0x0C084B48). Releasing the key that sounds releases the
   source (Roland's gate handling); releasing any other key of a chromatic bank does nothing. */
CHR(release) void chr_release(int pad) {
    int b = chr_keyof(pad);
    if (b >= 0) {
        if (chr_st.key[b] != pad + 1) return;
        pad = chr_tab.b[b].src - 1;
    }
    R_RELPAD(pad);
}
/* Audio: speed of a source's voice = the pad's speed x 2^(semitones/12) (octaves as exact shifts), at most just
   under 8x; exactly r for its own pitch and every other pad. Any stored byte gives a safe speed. */
CHR(ratio) static u32 chr_ratio(int pad, u32 r) {
    int b = chr_srcbank(pad), k, o = 0;
    if (b < 0) return r;
    k = chr_st.cur[b];
    if (!k || k > S0 + S_MAX) return r;
    k -= S0;                                                    /* semitones: -64..+35 */
    while (k < 0) { k += 12; o--; }
    while (k >= 12) { k -= 12; o++; }
    if (k) r = ((r >> 2) * ((chr_semi[k] + UNITY) >> 1)) >> 13; /* r < 2^18, factor < 2^17 */
    r = o < 0 ? r >> -o : r << o;
    return r > V_MAX ? V_MAX : r;
}

/* ------------------------------------------------------------------ mute groups */
/* A pad in a mute group 1..10 stops every other playing pad of its group, in any bank, when it starts (a choke).
   SHIFT + MARK on the play page opens a menu over it for the current pad (the last pad played): CTRL 1 sets its
   group, OFF fully left, then 1..10; the display shows OFF / M 1 .. M10 and MARK blinks. MARK closes it; the pad it
   edits plays and keeps it open; any other pad or button closes it and does what it does. Groups are saved in
   MUTEGRP.BIN (below). Roland's button -> LED table (0x0C138B74): MARK = button 36, LED 18. */
#define BTN_MARK 36
#define LED_MARK 18
#ifndef R_LED
#define R_LED ((void (*)(int led, int v))0x0C0AD8F4)   /* LED: 0 off, 1 on, 2 blink */
#endif
/* Display text: letter c, then n (1..59) in two places ("M 1", "M10", "L50"), in mute_st.txt (the mute menu and the
   pan menu, on different pages, are never shown at once). */
MUTE(num) static char *show_num(int c, int n) {
    char *t = mute_st.txt;
    int d = 0;
    while (n >= 10) { n -= 10; d++; }
    t[0] = (char)c; t[1] = d ? (char)('0' + d) : ' '; t[2] = (char)('0' + n); t[3] = 0;
    return t;
}
/* The menu's display and LED: the edited pad's group (OFF, M 1 .. M10), MARK blinking. */
MUTE(show) static void mute_show(void) {
    int g = mute_st.g[mute_st.pad];
    R_SHOWS(g ? show_num('M', g) : STR_OFF, 1);
    R_LED(LED_MARK, 2);
}
MUTE(close) static void mute_close(void) {
    mute_st.open = 0;
    R_LED(LED_MARK, 0);
}
/* Play page event hook (pev_stub), before chromatic mode's and Roland's. SHIFT + MARK opens the menu for the
   current pad; the MARK press that opens or closes it and its release are taken (Roland would start or end setting
   the pad's start / end points with them). In the menu: knobs are its own (CTRL 1: OFF, then groups 1..10); MARK
   closes it; the edited pad plays as usual; any other pad or button closes it first; releases and SHIFT: Roland's. */
MUTE(ev) int mute_page_event(void *page, struct event *e, evfn orig) {
    int id = e->id, pad;
    if (e->type == 0 && id == BTN_MARK && !e->val && mute_st.eat) { mute_st.eat = 0; return 0; }
    if (mute_st.open) {
        if (e->type != 0) {
            if (e->type == 1 && id == 0) {
                unsigned char *gp = &mute_st.g[mute_st.pad], g = (unsigned char)(((e->val < 0 ? 0 : e->val) * 11) >> 7);
                if (*gp != g) { *gp = g; mute_st.dirty = 1; }
                mute_show();
            }
            return 0;
        }
        if (e->val && id != BTN_SHIFT) {
            if (id == BTN_MARK) { mute_close(); mute_st.eat = 1; return 0; }
            if (!(id >= 1 && id <= 12 && R_GET(P_BANK, -1) * 12 + id - 1 == mute_st.pad)) mute_close();
        }
    } else if (e->type == 0 && e->id == BTN_MARK && e->val) {
        mute_st.eat = 0;                            /* a new press: a release still owed went to another page */
        if (R_HELD(BTN_SHIFT) && (pad = cur_pad()) >= 0) {
            mute_st.pad = (unsigned char)pad;
            mute_st.open = 1;
            mute_st.eat = 1;
            mute_show();
            return 0;
        }
    }
    return chr_page_event(page, e, orig);
}
/* Play page refresh hook (pref_stub): Roland's refresh and C.ON, then the menu over them while it's open. */
MUTE(refresh) int mute_page_refresh(void *page, pgfn orig) {
    int r = chr_page_refresh(page, orig);
    if (mute_st.open) mute_show();
    return r;
}
/* Pad `pad` starts: stop the other pads of its group that are playing, with Roland's stop sequence. */
MUTE(choke) static void mute_choke(int pad) {
    int g = mute_st.g[pad], p;
    if (!g) return;
    for (p = 0; p < 120; p++)
        if (p != pad && mute_st.g[p] == g && R_PLAYING(p)) chr_stop(p);
}

/* TIME/BPM page event hook (ev_stub): SHIFT + TIME/BPM (in the menu too: turns the bank on again, as from the
   play page), the chromatic menu, keys; everything else to vari_event. */
CAVE int vari_ev(struct page *p, struct event *e) {
    if (e->type == 0 && e->id == BTN_BPM && e->val && R_HELD(BTN_SHIFT) && !FWDMODE) { chr_open(p); return 1; }
    if (p->mode == MODE_CHR) return e->id == BTN_CANCEL ? chr_cancel(e) : chr_menu_ev(e);
    if (chr_padkey(e)) return 1;
    return vari_event(p, e);
}

/* ------------------------------------------------------------------ pan: menu */
/* SHIFT + START/END/LEVEL (from the main screen, or anywhere on the TIME/BPM page, the chromatic menu included; not in
   FWD mode, where it is Roland's BWD) opens a pan menu for the current pad (the last pad played): mode 10 of the
   TIME/BPM page. CTRL 3 sets the pad's pan: fully left L50, centre detent C (knob 62..65, as varispeed's), fully
   right R50; the display shows L50 .. L 1, C, R 1 .. R50, and only changes when CTRL 3 is turned. CTRL 1 / 2 do
   nothing. START/END/LEVEL blinks, TIME/BPM is off. START/END/LEVEL closes it for the main screen; the pad it edits,
   or a key of a chromatic bank, plays and keeps it; another pad closes it and Roland leaves the page for that pad;
   SHIFT + TIME/BPM switches to the chromatic menu; entering the play page closes it (chr_page_enter). */
/* Knob 0..127 -> pan -50..+50: 62 positions each side of the detent; (n * 50 + 61) / 62 as a multiply and shift
   (exact for n = 1..62), so both ends are 50 and every value is reached. */
PAN(pos) static int pan_pos(int v) {
    int n = v <= 61 ? 62 - (v < 0 ? 0 : v) : v >= 66 ? v - 65 : 0;
    if (!n) return 0;
    n = ((n * 50 + 61) * 1057) >> 16;
    return v <= 61 ? -n : n;
}
PAN(show) static void pan_show(void) {
    int v = pan_tab.v[chr_st.mpad];
    char *t = v < 0 ? show_num('L', -v) : show_num('R', v);   /* L50 .. L 1, R 1 .. R50 */
    if (!v) { t[0] = 'C'; t[1] = 0; }
    R_SHOWS(t, 1);
}
PAN(open) static void pan_open(struct page *p) {
    int pad = R_GET(P_CURPAD, -1);
    chr_st.want = 0;
    if (pad < 0 || pad >= 120) return;
    chr_st.mpad = (unsigned char)pad;
    chr_st.menu = 2;
    p->mode = MODE_PAN;
}
/* TIME/BPM page event hook (ev_stub): SHIFT + START/END/LEVEL opens the pan menu (in it too: it stays); in the pan
   menu everything but SHIFT + TIME/BPM is the menu's (1 = taken, 0 = Roland's); everything else to vari_ev. */
PAN(ev) int pan_event(struct page *p, struct event *e) {
    int id = e->id, pad;
    if (e->type == 0 && e->val && R_HELD(BTN_SHIFT) && !FWDMODE) {
        if (id == BTN_SEL) { pan_open(p); return 1; }
        if (id == BTN_BPM) return vari_ev(p, e);
    }
    if (p->mode != MODE_PAN) return vari_ev(p, e);
    if (e->type != 0) {
        if (e->type == 1 && id == 2) {
            s8 v = (s8)pan_pos(e->val), *pp = &pan_tab.v[chr_st.mpad];
            if (*pp != v) { *pp = v; pan_tab.dirty = 1; }
            pan_show();
        }
        return 1;                                   /* CTRL 1 / 2: nothing */
    }
    if (!e->val || id == BTN_SHIFT) return 0;       /* releases and SHIFT: Roland's */
    if (id == BTN_SEL) { chr_st.menu = 0; R_SETPAGE(0, 1); return 1; }
    if (id >= 1 && id <= 12) {
        pad = R_GET(P_BANK, -1) * 12 + id - 1;
        if (pad == chr_st.mpad) return 0;           /* plays; Roland keeps the page for its own pad */
        if (chr_keyof(pad) >= 0) return 1;          /* a chromatic key: plays, the page stays */
    }
    chr_st.menu = 0;                                /* another pad or button: the menu closes, then Roland's */
    return 0;
}
/* TIME/BPM page enter and refresh (pan_enter_stub / pan_refresh_stub): Roland's, which draws the display (our menus)
   and then the TIME/BPM / START/END/LEVEL LEDs from the mode; in the pan menu START/END/LEVEL blinks instead. */
PAN(led) int pan_led(struct page *p, pgfn orig) {
    int r = orig(p);
    if (p->mode == MODE_PAN && chr_st.menu == 2) { R_LED(LED_BPM, 0); R_LED(LED_SEL, 2); }
    return r;
}

/* ------------------------------------------------------------------ audio */
typedef int (*fillfn)(void *obj, s16 *L, s16 *R, int *flag, int *frames);

#define OBJ_I(o, off) (*(int *)((char *)(o) + (off)))
#define O_VOICE  24
#define O_PAD    28
#define O_CURSOR 16

static inline s16 lerp(int a, int b, u32 ph) {        /* ph: Q16 fraction < 1 */
    return (s16)(a + (((b - a) * (int)(ph >> 1)) >> 15));
}

/* Produce m outputs into oL/oR from source frames sL/sR (consumed in order).
   lim = source frames that are real audio (the rest is silence after the sample end).
   *bad = first output index that needed a frame beyond lim (unchanged if none).
   Returns frames consumed. Safe in place for the layouts used below: every source frame is
   read before the output slot at the same address is written. */
static int run(struct vstate *s, s16 *oL, s16 *oR, const s16 *sL, const s16 *sR,
               int m, u32 r, int lim, int *bad) {
    int k = 0;
    u32 ph = s->ph;
    int aL = s->aL, aR = s->aR, bL = s->bL, bR = s->bR;
    for (int j = 0; j < m; j++) {
        while (ph >= UNITY) {
            ph -= UNITY;
            aL = bL; aR = bR;
            bL = sL[k]; bR = sR[k];
            k++;
        }
        if (k > lim && *bad < 0) *bad = j;
        oL[j] = lerp(aL, bL, ph);
        oR[j] = lerp(aR, bR, ph);
        ph += r;
    }
    s->ph = ph; s->aL = aL; s->aR = aR; s->bL = bL; s->bR = bR;
    return k;
}

static int frames_needed(u32 ph, int m, u32 r) {        /* floor(ph + (m-1)*r): frames consumed by m outputs */
    return (int)((ph + (u32)(m - 1) * r) >> 16);
}

/* Card streaming. A long sample plays from its RAM head, then from two 24 KB card buffers in turn; Roland's fill
   ends the voice when the buffer it needs isn't loaded, and flags a finished buffer in *flag (one bit per call).
   Its caller refills that buffer after the call: directly during playback, but during the 4-5 blocks a voice start
   renders at once only through the sample task's queue, run after the start, as is the second buffer's first load.
   Faster than ~1.4x (stereo) a start runs out of data and the note stops after its head (issue #1); faster than
   1.5x a block can also finish two buffers. So a faster voice refills a finished buffer itself, right after the
   fill call, with the call Roland's playback service makes there (nothing is left in *flag for the caller), and
   loads the second buffer at the note's start. Normal speed and slower: Roland's own handling, as before. */
#ifndef R_REFILL
#define R_REFILL ((int (*)(void *feeder))0x0C0911B4)   /* refill the buffer the last fill finished (synchronous) */
#endif
#define O_CHUNKS 8     /* feeder: card chunks after the head (refills only exist above 2)                    */
#define O_LOAD1  48    /* feeder (forward): second buffer's first load still to be queued and run            */
/* After a fill call: refill the buffer it finished. No flag: a faster note starts, and can reach the second buffer
   before Roland's queued load of it runs; load it now with a refill instead, as Roland does for pads it streams
   synchronously (cleared, the start doesn't queue the load). */
LED __attribute__((noinline)) static void stream_refill(void *obj, int *flag) {
    if (flag) {
        if (!*flag) return;
        *flag = 0;
    } else {
        if (!OBJ_I(obj, O_LOAD1)) return;
        OBJ_I(obj, O_LOAD1) = 0;
    }
    R_REFILL(obj);
}

/* Fetch n source frames into sL/sR: from Roland's fill until the sample ends, silence after. */
static inline __attribute__((always_inline)) void fetch(void *obj, s16 *sL, s16 *sR, int n, int *flag, fillfn orig,
                                                        u32 r, int *ended, int *real) {
    *real = n;
    if (n <= 0) return;
    if (!*ended) {
        int c = n;
        if (orig(obj, sL, sR, flag, &c) == 0) { *ended = 1; *real = c; }
        if (r > UNITY) stream_refill(obj, flag);
    } else *real = 0;
    for (int i = *real; i < n; i++) { sL[i] = 0; sR[i] = 0; }
}

int vari_fill(void *obj, s16 *L, s16 *R, int *flag, int *frames, fillfn orig) {
    int vi = OBJ_I(obj, O_VOICE), pad = OBJ_I(obj, O_PAD), F = *frames;
    if (vi < 0 || vi >= MAXV || pad < 0 || pad >= 120 || F <= 0)
        return orig(obj, L, R, flag, frames);
    struct vstate *s = &vari_vs[vi];
    u32 r = chr_ratio(pad, pad_ratio(pad));
    if (r == UNITY) {                                    /* normal speed: exactly stock */
        s->nxt = 0;
        return orig(obj, L, R, flag, frames);
    }
    if (OBJ_I(obj, O_CURSOR) + 1 != s->nxt) {          /* new note, loop restart, or speed just enabled */
        s->aL = s->aR = s->bL = s->bR = 0;
        s->ph = 2 * UNITY;                               /* consume 2 frames before the first output */
        if (r > UNITY && OBJ_I(obj, O_CHUNKS) > 2)
            stream_refill(obj, 0);                       /* faster: the second buffer, now */
    }
    int o = 0, ended = 0, bad = -1;
    s16 tL[8], tR[8];
    while (o < F) {
        int left = F - o, m = 0, n = 0, place = 0;
        if (r < UNITY) {                                 /* slower: source at the tail, one pass */
            m = left; n = frames_needed(s->ph, m, r); place = F - n;
            if (n > left) m = 0;
        } else {                                         /* faster: source one frame ahead, several passes */
            int cap = left - 1;
            if (cap >= 4 && (u32)cap * UNITY >= s->ph) {
                m = R_DIV((int)((u32)cap * UNITY - s->ph), (int)r) + 1;
                if (m > left) m = left;
                n = frames_needed(s->ph, m, r);
                while (m > 0 && n > cap) { m--; n = frames_needed(s->ph, m, r); }
                place = o + 1;
            }
        }
        int real, b = -1;
        if (m <= 0) {                                    /* last few outputs: one at a time via tL/tR */
            n = (int)(s->ph >> 16);
            if (n > 8) n = 8;                            /* ph < UNITY + r before an output: <= 8 for r < 8x */
            fetch(obj, tL, tR, n, flag, orig, r, &ended, &real);
            run(s, L + o, R + o, tL, tR, 1, r, real, &b);
            m = 1;
        } else {
            fetch(obj, L + place, R + place, n, flag, orig, r, &ended, &real);
            run(s, L + o, R + o, L + place, R + place, m, r, real, &b);
        }
        if (ended && bad < 0 && b >= 0) bad = o + b;
        o += m;
    }
    s->nxt = OBJ_I(obj, O_CURSOR) + 1;
    if (ended) { *frames = bad < 0 ? F : bad; return 0; }
    return 1;
}

/* ------------------------------------------------------------------ pan: sound */
/* Every voice's fill (fill_fwd / fill_rev, in place of vari_fill): vari_fill as before, then the pad's pan on the frames
   it made: balance law. Panned left, the right channel goes down linearly to silence at L50 and the left one stays as
   it is; panned right, the mirror. No boost anywhere. Centre: nothing touched (stock, bit for bit, as vari_fill
   leaves it). The pad in the feeder is a chromatic key's source, so keys use the source's pan. Q15 gain
   (50 - |pan|) * 655: 0.98 at L 1, 0 at L50.
   A mono pad plays one channel; one panned when it starts is started as two (pan_channels, vstart_stub) and its right
   channel is made here from the left one (Roland's mono copy fills only L): the near side at MONO_TRIM, the far side
   at MONO_TRIM x the gain. A voice keeps its channel count while it plays (a pad panned from C while it plays is panned
   from its next start). */
#ifndef PADINFO
#define PADINFO  ((const unsigned char *)0x0C179344)
#endif
#ifndef VOICES
#define VOICES   ((const unsigned char *)0x0CCAE0C8)   /* Roland's voice objects, 24 B each: +8 player (+4 channels) */
#endif
#ifndef MONO_TRIM
#define MONO_TRIM 32768           /* a panned mono pad's near side, Q15: 32768 = unity, 23170 = -3 dB */
#endif
/* Voice start (vstart_stub, Roland's 0x0C0963AC with r5 = the pad's channels): the channels to start it with. */
PAN(vch) int pan_channels(int ch, int pad) {
    return ch == 1 && (unsigned)pad < 120 && pan_tab.v[pad] ? 2 : ch;
}
PAN(fill) int pan_fill(void *obj, s16 *L, s16 *R, int *flag, int *frames, fillfn orig) {
    int r = vari_fill(obj, L, R, flag, frames, orig), pad = OBJ_I(obj, O_PAD), vi = OBJ_I(obj, O_VOICE), v, n, g;
    s16 *x;
    if ((unsigned)pad >= 120) return r;
    v = pan_tab.v[pad];
    n = *frames;
    g = (50 - (v < 0 ? -v : v)) * 655;
    if (PADINFO[pad * 32 + 22] != 2) {                       /* mono: only a voice started as two channels */
        if ((unsigned)vi >= MAXV || *(const int *)(*(const char *const *)(VOICES + 24 * vi + 8) + 4) != 2) return r;
        if (MONO_TRIM < 32768) g = (g * MONO_TRIM) >> 15;
        for (; n > 0; n--, L++, R++) {
            int a = *L, b = (a * (short)g) >> 15;
            if (MONO_TRIM < 32768) a = (a * (short)(MONO_TRIM & 0x7FFF)) >> 15;
            if (!v) b = a;
            *L = (s16)(v > 0 ? b : a);
            *R = (s16)(v > 0 ? a : b);
        }
        return r;
    }
    if (!v) return r;                                        /* stereo at centre: untouched */
    for (x = v < 0 ? R : L; n > 0; n--, x++) *x = (s16)((*x * (short)g) >> 15);
    return r;
}

/* ------------------------------------------------------------------ persistence */
#ifndef R_OPEN
#define R_LOCK   ((void (*)(int))0x0C0EE838)
#define R_UNLOCK ((void (*)(int))0x0C0EE844)
#define R_CREATE ((int  (*)(const char *))0x0C0D2A04)
#define R_OPEN   ((int  (*)(const char *, int))0x0C0D2940)
#define R_READ   ((int  (*)(int, void *, int))0x0C0D2A80)
#define R_WRITE  ((int  (*)(int, const void *, int))0x0C0D2B30)
#define R_CLOSE  ((void (*)(int))0x0C0D2B8C)
#define R_CARDOK ((int  (*)(void))0x0C0EF484)
#define PADINFO  ((const unsigned char *)0x0C179344)
#endif
#ifndef PERSIST
#define PERSIST __attribute__((section(".text.persist")))
#endif
static const char vari_path[] __attribute__((section(".rodata.wsp"))) = "/ROLAND/SP-404SX/SMPL/VARISPD.BIN";

/* 1-byte fingerprint of the sample on a pad: data end (= length), channels, format. 0 = empty pad. */
__attribute__((noinline, section(".text.helper"))) static unsigned char pad_fp(int pad) {
    const unsigned char *r = PADINFO + pad * 32;
    u32 end = *(const u32 *)(r + 4);
    if (end <= 512) return 0;
    u32 h = end ^ (end >> 8) ^ (end >> 16) ^ (end >> 24) ^ ((u32)r[21] << 3) ^ ((u32)r[22] << 5);
    h &= 0xFF;
    return h ? (unsigned char)h : 1;
}

/* Bring the table in line with PAD_INFO: a pad whose sample fingerprint changed takes the speed of the
   pad that held that sample before (copy, exchange), otherwise resets to normal. Returns 1 if anything changed. */
PERSIST __attribute__((noinline)) static int resync(void) {
    s8 np[120];
    struct vtab *t = &vari_tab;
    int p, q, changed = 0;
    for (p = 0; p < 120; p++) {
        unsigned char f = pad_fp(p);
        np[p] = -128;
        if (f == t->e[p].fp) continue;
        np[p] = 0;
        if (f) for (q = 0; q < 120; q++)
            if (q != p && t->e[q].fp == f) { np[p] = t->e[q].pos; break; }
    }
    for (p = 0; p < 120; p++)
        if (np[p] != -128) { t->e[p].pos = np[p]; t->e[p].fp = pad_fp(p); changed = 1; }
    return changed;
}

/* Write our file (same lock as Roland's PAD_INFO save). */
__attribute__((noinline, section(".text.fp"))) static void vari_save(void) {
    R_LOCK(14);
    int fd = R_CREATE(vari_path);
    if (fd >= 0) { R_WRITE(fd, &vari_tab, sizeof vari_tab); R_CLOSE(fd); }
    R_UNLOCK(14);
}

/* Every 100 ms from Roland's background loop, after its PAD_INFO auto-save. Our file is written once
   nothing has changed for a second (same timing as Roland's own auto-save). */
PERSIST int vari_tick(int ret, int ms) {
    struct vtab *t = &vari_tab;
    if (vari_need_load) {                       /* PAD_INFO was (re)loaded: read our file, never write here */
        int n = 0, fd;
        vari_need_load = 0;
        R_LOCK(14);
        fd = R_OPEN(vari_path, 1);
        if (fd >= 0) { n = R_READ(fd, t, sizeof *t); R_CLOSE(fd); }
        R_UNLOCK(14);
        if (n != (int)sizeof *t || t->magic != 0x56535031) {              /* missing/invalid: start fresh */
            u32 *w = (u32 *)t;
            for (unsigned i = 0; i < sizeof *t / 4; i++) w[i] = 0;
            t->magic = 0x56535031;                                       /* "VSP1" */
        }
        resync();
        vari_dirty_ms = 0;
        return ret;
    }
    if (resync()) vari_dirty_ms = 1;
    if (vari_dirty_ms > 0) {
        vari_dirty_ms += ms;
        if (vari_dirty_ms > 1000 && R_CARDOK()) {
            vari_save();
            vari_dirty_ms = 0;
        }
    }
    return ret;
}

/* Roland's pad copy (exch = 0) and exchange (1) workers (xcopy_stub / xexch_stub), run by its file task with the
   pads in params 60 (source) and 61 (destination); 0 = done. Speed, mute group and pan move with the sample
   explicitly: the fingerprints can't tell identical samples apart (a copy exchanged with its source, a copy over
   another copy). They are read before the job, as a tick may run on the half-done PAD_INFO meanwhile. */
CHR(xset) static void vari_xset(int pad, int pos) {
    vari_tab.e[pad].pos = (s8)pos;
    vari_tab.e[pad].fp = pad_fp(pad);
    vari_dirty_ms = 1;
}
CHR(xop) int vari_xop(int (*fn)(void), int exch) {
    int p[2], i, r;
    s8 s[2], grp[2], pan[2];
    for (i = 0; i < 2; i++) {
        p[i] = R_GET(60 + i, -1);
        if ((unsigned)p[i] >= 120) return fn();
        s[i] = vari_tab.e[p[i]].pos;
        grp[i] = (s8)mute_st.g[p[i]];
        pan[i] = pan_tab.v[p[i]];
    }
    r = fn();
    if (!(r & 0xFF)) {
        for (i = !exch; i < 2; i++) {
            vari_xset(p[i], s[i ^ 1]);
            mute_st.g[p[i]] = (unsigned char)grp[i ^ 1];
            pan_tab.v[p[i]] = pan[i ^ 1];
        }
        mute_st.dirty = 1;
        pan_tab.dirty = 1;
    }
    return r;
}
#define mute_xop vari_xop                           /* its name in the mute group builds */

/* ------------------------------------------------------------------ chromatic mode: saving */
static const char chr_path[] SECT(".rodata.chr") = "/ROLAND/SP-404SX/SMPL/CHROMA.BIN";

/* Turn off every bank whose entry doesn't fit (source not in its bank, bad channel / octave) or whose source
   sample changed (deleted, replaced, recorded over). Returns 1 if any was. */
CHR(check) static int chr_check(void) {
    int b, any = 0;
    for (b = 0; b < 10; b++) {
        struct cbank *c = &chr_tab.b[b];
        if (c->src && (c->src > 120 || chr_bankof(c->src - 1) != b || c->ch > 15 || (c->mode & 7) > 4 || c->mode > 15 ||
                       pad_fp(c->src - 1) != c->fp)) { c->src = 0; any = 1; }
    }
    return any;
}
/* Read (write = 0) or write one of our files under lock 14, like Roland's PAD_INFO: n bytes from / to a, then (if all
   of those moved) m bytes from / to b. Returns the bytes moved, -1 if the file can't be opened or created. */
static int file_io(const char *path, int write, void *a, int n, void *b, int m);
/* Read CHROMA.BIN after PAD_INFO was (re)loaded; a missing or invalid file starts with every bank off. */
CHR(load) static void chr_load(void) {
    struct ctab *t = &chr_tab;
    int n = file_io(chr_path, 0, t, sizeof *t, 0, 0), b;
    if (n != (int)sizeof *t || t->magic != CHR_MAGIC) {
        u32 *w = (u32 *)t;
        for (b = 0; b < (int)(sizeof *t / 4); b++) w[b] = 0;
        t->magic = CHR_MAGIC;
    }
    chr_check();
    for (b = 0; b < 10; b++) chr_st.cur[b] = 0;
    chr_st.dirty = 0;
}
CHR(save) static void chr_save(void) {
    file_io(chr_path, 1, &chr_tab, sizeof chr_tab, 0, 0);
}
/* Every 100 ms (tick_stub): Roland's auto-save and VARISPD.BIN first, then CHROMA.BIN the same way: loaded with it,
   a bank whose source sample changed (deleted, replaced, recorded over) turns off, written 1 s after a change. */
static int due(volatile int *d, int ms);
CHR(tick) int chr_tick(int ret, int ms) {
    int load = vari_need_load;
    ret = vari_tick(ret, ms);
    if (load) { chr_load(); return ret; }
    if (chr_check()) chr_st.dirty = 1;
    if (due(&chr_st.dirty, ms)) chr_save();
    return ret;
}

/* ------------------------------------------------------------------ mute groups and pan: saving */
/* MUTEGRP.BIN / PAN.BIN: a magic, per pad its value (mute group 0..10 / pan -50..+50), then per pad the fingerprint of
   the sample the value was set with (244 B). Read after PAD_INFO was (re)loaded, written 1 s after a change, like
   VARISPD.BIN and CHROMA.BIN. A pad whose sample changes (deleted, recorded over, imported) goes back to 0 (no group,
   centre); copy and exchange take the value along. Both tables start like struct stab (mute_st, pan_tab). */
struct stab { u32 magic; s8 v[120]; };
#define TAB_HEAD   124             /* magic + values */
#define MUTE_MAGIC 0x4D555431u     /* "MUT1" */
#define MUTE_HEAD  TAB_HEAD
#define PAN_MAGIC  0x50414E31u     /* "PAN1" */
#define MUTE_T     ((struct stab *)&mute_st)
#define PAN_T      ((struct stab *)&pan_tab)
static const char mute_path[] __attribute__((section(".rodata.mute"))) = "/ROLAND/SP-404SX/SMPL/MUTEGRP.BIN";
static const char pan_path[] __attribute__((section(".rodata.mute"))) = "/ROLAND/SP-404SX/SMPL/PAN.BIN";
MUTE(file) static int file_io(const char *path, int write, void *a, int n, void *b, int m) {
    int fd, k = -1;
    R_LOCK(14);
    fd = write ? R_CREATE(path) : R_OPEN(path, 1);
    if (fd >= 0) {
        k = write ? R_WRITE(fd, a, n) : R_READ(fd, a, n);
        if (m && (write || k == n)) k += write ? R_WRITE(fd, b, m) : R_READ(fd, b, m);
        R_CLOSE(fd);
    }
    R_UNLOCK(14);
    return k;
}
/* A missing or invalid file, a value outside lo..hi, or a pad whose sample isn't the one the value was set with: 0. */
MUTE(load) static void tab_load(struct stab *t, const char *path, u32 magic, int lo, int hi) {
    unsigned char fp[120];
    int n = file_io(path, 0, t, TAB_HEAD, fp, 120), p, v;
    for (p = 0; p < 120; p++) {
        v = t->v[p];
        if (n != TAB_HEAD + 120 || t->magic != magic || v < lo || v > hi || !fp[p] || fp[p] != pad_fp(p)) t->v[p] = 0;
    }
    t->magic = magic;
}
MUTE(save) static void tab_save(const struct stab *t, const char *path) {
    unsigned char fp[120];
    int p;
    for (p = 0; p < 120; p++) fp[p] = pad_fp(p);
    file_io(path, 1, (void *)t, TAB_HEAD, fp, 120);
}
/* An unsaved change (*d > 0): count the ms since it; 1 = write the file now (1 s after the last change, with the card
   ready). The timer restarts before the write, so a change made while it runs is written next time. */
MUTE(due) static int due(volatile int *d, int ms) {
    if (*d <= 0) return 0;
    *d += ms;
    if (*d <= 1000 || !R_CARDOK()) return 0;
    *d = 0;
    return 1;
}
/* Every 100 ms (tick_stub), around chromatic mode's and varispeed's: both tables. */
MUTE(tick) int mute_tick(int ret, int ms) {
    int load = vari_need_load, p;
    if (!load)                                      /* a pad whose sample changed since varispeed last looked (its */
        for (p = 0; p < 120; p++)                   /* fingerprint, which varispeed's resync then updates): 0 */
            if ((mute_st.g[p] || pan_tab.v[p]) && pad_fp(p) != vari_tab.e[p].fp) {
                if (mute_st.g[p]) { mute_st.g[p] = 0; mute_st.dirty = 1; }
                if (pan_tab.v[p]) { pan_tab.v[p] = 0; pan_tab.dirty = 1; }
            }
    ret = chr_tick(ret, ms);
    if (load) {
        tab_load(MUTE_T, mute_path, MUTE_MAGIC, 0, 10);
        tab_load(PAN_T, pan_path, PAN_MAGIC, -50, 50);
        mute_st.dirty = 0;
        pan_tab.dirty = 0;
        return ret;
    }
    if (due(&mute_st.dirty, ms)) tab_save(MUTE_T, mute_path);
    if (due(&pan_tab.dirty, ms)) tab_save(PAN_T, pan_path);
    return ret;
}

