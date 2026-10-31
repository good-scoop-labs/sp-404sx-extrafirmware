/* SP-404SX/A EXTRAfirmware: per-pad tape-style varispeed for the Roland SP-404SX / SP-404A (system 1.07).
   - TIME/BPM page, CTRL 3: per-pad speed (fully left OFF, -12 .. +7 semitones, fully right PTN = follow the
     pattern tempo, 0.25x .. 4x). The display shows the tempo you hear; CTRL 1 / CTRL 2 show their value x speed.
   - Audio: a wrapper around every sample feeder's fill() asks Roland's fill for floor(phase + F*ratio) source
     frames and resamples them (linear interpolation) in place into the F-frame block. Normal speed calls
     Roland's fill directly (bit-identical to stock).
   - Saving: speeds are kept in VARISPD.BIN on the card and follow the sample (copy / exchange / delete).
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
#define R_MAX    0x3FFFFu           /* .. just under 4x (+24 st); the resampler supports any ratio < 4 */


static int cur_pad(void) {
    int pad = R_GET(P_CURPAD, -1);
    return (pad >= 0 && pad < 120) ? pad : -1;
}
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
CAVE static void vari_show(void) {
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
#define BTN_BPM  40                    /* the only button whose stock handling differs between modes 7 and 8 */
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

/* Fetch n source frames into sL/sR: from Roland's fill until the sample ends, silence after. */
static void fetch(void *obj, s16 *sL, s16 *sR, int n, int *flag, fillfn orig, int *ended, int *real) {
    *real = n;
    if (n <= 0) return;
    if (!*ended) {
        int c = n;
        if (orig(obj, sL, sR, flag, &c) == 0) { *ended = 1; *real = c; }
    } else *real = 0;
    for (int i = *real; i < n; i++) { sL[i] = 0; sR[i] = 0; }
}

int vari_fill(void *obj, s16 *L, s16 *R, int *flag, int *frames, fillfn orig) {
    int vi = OBJ_I(obj, O_VOICE), pad = OBJ_I(obj, O_PAD), F = *frames;
    if (vi < 0 || vi >= MAXV || pad < 0 || pad >= 120 || F <= 0)
        return orig(obj, L, R, flag, frames);
    struct vstate *s = &vari_vs[vi];
    u32 r = pad_ratio(pad);
    if (r == UNITY) {                                    /* normal speed: exactly stock */
        s->nxt = 0;
        return orig(obj, L, R, flag, frames);
    }
    if (OBJ_I(obj, O_CURSOR) + 1 != s->nxt) {          /* new note, loop restart, or speed just enabled */
        s->aL = s->aR = s->bL = s->bR = 0;
        s->ph = 2 * UNITY;                               /* consume 2 frames before the first output */
    }
    int o = 0, ended = 0, bad = -1;
    s16 tL[4], tR[4];
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
            if (n > 4) n = 4;
            fetch(obj, tL, tR, n, flag, orig, &ended, &real);
            run(s, L + o, R + o, tL, tR, 1, r, real, &b);
            m = 1;
        } else {
            fetch(obj, L + place, R + place, n, flag, orig, &ended, &real);
            run(s, L + o, R + o, L + place, R + place, m, r, real, &b);
        }
        if (ended && bad < 0 && b >= 0) bad = o + b;
        o += m;
    }
    s->nxt = OBJ_I(obj, O_CURSOR) + 1;
    if (ended) { *frames = bad < 0 ? F : bad; return 0; }
    return 1;
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
