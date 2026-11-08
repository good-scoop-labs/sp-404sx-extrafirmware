/* Host test: mute groups (SHIFT + MARK menu on the play page, CTRL 1 OFF / 1..10, MARK blinking, closing by MARK or
   another pad / button, the edited pad playing, choke on every pad start). Roland functions are mocks; vari.c is
   compiled for the PC. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int prm[64], patt=1200; static char shbuf[8]; static int shows;
static int mock_cur=26, mock_bank=2;
static int g(int id,int pad){ if(id==52) return mock_cur; if(id==53) return mock_bank; return prm[id]; }
static int dv(int a,int b){return a/b;}
static int pt(void){ return patt*10; }
static void sn(int n){}
static void ss(const char*s,int f){ shows++; if((unsigned long)s>=0x0C080000 && (unsigned long)s<0x0C140000) strcpy(shbuf,"(rol)"); else strncpy(shbuf,s,7); }   /* Roland's own strings (OFF): not on the PC */
#define R_GET g
#define R_DIV dv
#define R_SHOWN sn
#define R_SHOWS ss
static short knobs[3];
#define KNOBPOS knobs
#define R_TEMPO pt
#define DEV
#define SECT(x)
#define CAVE
#define PERSIST
#define section(x) unused
static unsigned char padinfo[120*32];
#define PADINFO ((const unsigned char*)padinfo)
/* a tiny card: a few files, each with a read / write position */
static char fname[4][40]; static unsigned char fdata[4][512]; static int flen[4], fpos[4], fwrites;
static int slot_of(const char*p){ for(int i=0;i<4;i++) if(!strcmp(fname[i],p)) return i; for(int i=0;i<4;i++) if(!fname[i][0]){ strcpy(fname[i],p); return i; } return 0; }
static void lk(int n){}
static int cr(const char*p){ int s=slot_of(p); flen[s]=0; fpos[s]=0; return s; }
static int op(const char*p,int m){ for(int i=0;i<4;i++) if(!strcmp(fname[i],p) && flen[i]) { fpos[i]=0; return i; } return -1; }
static int rd(int f,void*b,int n){ int k=flen[f]-fpos[f]; if(k>n) k=n; if(k<0) k=0; memcpy(b,fdata[f]+fpos[f],k); fpos[f]+=k; return k; }
static int wr(int f,const void*b,int n){ memcpy(fdata[f]+fpos[f],b,n); fpos[f]+=n; if(fpos[f]>flen[f]) flen[f]=fpos[f]; fwrites++; return n; }
static void cl(int f){} static int cardok(void){return 1;}
#define R_LOCK lk
#define R_UNLOCK lk
#define R_CREATE cr
#define R_OPEN op
#define R_READ rd
#define R_WRITE wr
#define R_CLOSE cl
#define R_CARDOK cardok
#include "mock_chr.h"
/* several pads playing at once (mock_chr.h tracks only one) */
static unsigned char playing[120]; static int stopped[120], stops_total;
static int multi_playing(int pad){ return pad >= 0 && pad < 120 && playing[pad]; }
static void multi_stop(int pad, int how){ if (pad >= 0 && pad < 120) { playing[pad] = 0; stopped[pad]++; } stops_total++; }
static int started = -1;
static int multi_playpad(int kind, int pad, int vel){ started = pad; playing[pad] = 1; return 1; }
#undef R_PLAYING
#undef R_STOPPAD
#undef R_PLAYPAD
#define R_PLAYING multi_playing
#define R_STOPPAD multi_stop
#define R_PLAYPAD multi_playpad
#include "vari.c"
#include "ratio.c"
static unsigned mock_ratio_now(int pad) { return chr_ratio(pad, pad_ratio(pad)); }

static int fails=0;
#define CHECK(c, what) do { int ok_=(c); printf("%s: %s\n", ok_?"ok  ":"FAIL", what); if(!ok_) fails++; } while(0)
static int pevs; static struct event last; static int pev_orig(void *page, struct event *e){ pevs++; last = *e; return 0; }
/* an event on the play page; returns 1 if Roland got it */
static int ev(int type, int id, int val){ struct event e={type,(unsigned char)id,(signed char)val}; int n=pevs; mock_page[0]=VT_PLAY; mute_page_event(0,&e,pev_orig); return pevs>n; }
static int btn(int id, int val){ return ev(0,id,val); }
static int knob(int id, int val){ return ev(1,id,val); }
static int open_menu(int pad){ int r; mock_cur=pad; mock_shift=1; r=btn(BTN_MARK,1); mock_shift=0; return r; }
static int refreshes; static int mock_pg(void *page){ refreshes++; return 5; }
/* saving */
static const char *MPATH = "/ROLAND/SP-404SX/SMPL/MUTEGRP.BIN";
static void setfp(int pad, unsigned end){ *(unsigned*)(padinfo+pad*32+4)=end; }   /* the pad's sample length (0: empty) */
static void ticks(int n){ while(n--) mute_tick(0,100); }                           /* 100 ms background ticks */
static int file_of(const char*p){ for(int i=0;i<4;i++) if(!strcmp(fname[i],p) && flen[i]) return i; return -1; }
static void set_group(int pad, int knobval){ open_menu(pad); btn(BTN_MARK,0); knob(0,knobval); btn(BTN_MARK,1); btn(BTN_MARK,0); }
/* Roland's copy / exchange jobs (pads in params 60 / 61): move PAD_INFO; 0 = done */
static int job_src, job_dst, job_calls;
static int job_copy(void){ memcpy(padinfo+job_dst*32, padinfo+job_src*32, 32); return 0; }
static int job_exch(void){ unsigned char t[32]; memcpy(t,padinfo+job_dst*32,32); memcpy(padinfo+job_dst*32,padinfo+job_src*32,32); memcpy(padinfo+job_src*32,t,32); return 0; }
static int job_fail(void){ return 1; }
static int job_noop(void){ job_calls++; return 0; }
static int job_copy_tick(void){ job_copy(); ticks(1); return 0; }   /* the background tick runs meanwhile */
static void refresh(void){ shbuf[0]=0; mute_page_refresh(0, mock_pg); }

int main(){
  /* ---- opening */
  mock_cur=-1; mock_shift=1;
  CHECK(btn(BTN_MARK,1) && !mute_st.open, "SHIFT + MARK with no current pad: Roland's, no menu");
  CHECK(btn(BTN_MARK,0), "  ... and its release too");
  mock_shift=0;
  CHECK(btn(BTN_MARK,1) && btn(BTN_MARK,0) && !mute_st.open, "MARK alone: Roland's (start / end points), no menu");
  shows=0;
  CHECK(!open_menu(26) && mute_st.open && mute_st.pad==26, "SHIFT + MARK: menu open for the current pad, not passed to Roland");
  CHECK(mock_led[LED_MARK]==2, "  MARK blinks");
  CHECK(shows==1 && !strcmp(shbuf,"(rol)"), "  shows OFF (Roland's text) for a pad with no group");
  CHECK(!btn(BTN_MARK,0), "  the MARK release is taken");
  CHECK(btn(BTN_SHIFT,0), "  SHIFT release: Roland's");

  /* ---- CTRL 1: OFF fully left, then 1..10 */
  int lo[11], hi[11], mono=1, prev=0;
  for(int i=0;i<11;i++){ lo[i]=999; hi[i]=-1; }
  for(int v=0; v<128; v++){
    int r = knob(0, v), gr = mute_st.g[26];
    if (r) { CHECK(0, "a CTRL 1 move reached Roland"); break; }
    if (gr < prev) mono = 0;
    prev = gr;
    if (gr <= 10) { if (v < lo[gr]) lo[gr]=v; if (v > hi[gr]) hi[gr]=v; }
  }
  CHECK(mono, "CTRL 1: the group only goes up as the knob turns right");
  int all=1; for(int i=0;i<11;i++) if(hi[i]<0) all=0;
  CHECK(all, "CTRL 1: OFF and every group 1..10 can be reached");
  CHECK(lo[0]==0 && hi[0]<=12 && hi[10]==127, "CTRL 1: OFF only at the left end, 10 at the right end");
  int zone=1; for(int i=1;i<11;i++) if(hi[i]-lo[i] < 9) zone=0;
  CHECK(zone, "CTRL 1: each group has a zone of at least 10 positions");
  knob(0, 0);  CHECK(mute_st.g[26]==0 && !strcmp(shbuf,"(rol)"), "CTRL 1 fully left: OFF");
  knob(0, 20); CHECK(mute_st.g[26]==1 && !strcmp(shbuf,"M 1"), "CTRL 1: group 1 shows M 1");
  knob(0, 127); CHECK(mute_st.g[26]==10 && !strcmp(shbuf,"M10"), "CTRL 1 fully right: group 10 shows M10");
  knob(0, 70); int g70 = mute_st.g[26];
  CHECK(g70>=5 && g70<=6 && shbuf[0]=='M' && shbuf[1]==' ' && shbuf[2]=='0'+g70, "CTRL 1 middle: M 5 / M 6");
  CHECK(!knob(1, 100) && !knob(2, 100) && mute_st.g[26]==g70, "CTRL 2 / 3: taken by the menu, group unchanged");
  CHECK(mute_st.open, "  menu still open");

  /* ---- refresh: Roland's, then the menu over it */
  refreshes=0; mock_led[LED_MARK]=0; refresh();
  CHECK(refreshes==1 && shbuf[0]=='M' && mock_led[LED_MARK]==2, "refresh: Roland's first, then the menu text and MARK blinking");

  /* ---- pads and buttons while open */
  mock_bank=2;                                   /* pad 26 = bank C pad 3 */
  CHECK(btn(3,1) && mute_st.open, "the edited pad: plays (Roland's), menu stays open");
  CHECK(btn(3,0), "  its release: Roland's");
  mock_shift=1; CHECK(btn(BTN_SHIFT,1) && mute_st.open, "SHIFT press: Roland's, menu stays open"); mock_shift=0;
  CHECK(btn(4,1) && !mute_st.open, "another pad: menu closes and the pad plays (Roland's)");
  CHECK(mock_led[LED_MARK]==0, "  MARK stops blinking");
  refresh(); CHECK(shbuf[0]==0, "  closed: refresh draws nothing of ours");
  CHECK(btn(4,0), "  its release: Roland's");

  open_menu(26); btn(BTN_MARK,0);
  mock_bank=3;                                   /* same pad number, other bank: another pad */
  CHECK(btn(3,1) && !mute_st.open, "the same pad button in another bank: another pad, menu closes");
  mock_bank=2;

  open_menu(26); btn(BTN_MARK,0);
  CHECK(!btn(BTN_MARK,1) && !mute_st.open && mock_led[LED_MARK]==0, "MARK again: menu closes, not passed to Roland");
  CHECK(!btn(BTN_MARK,0), "  its release is taken too");
  CHECK(btn(BTN_MARK,1) && btn(BTN_MARK,0), "  after that MARK is Roland's again");

  open_menu(26); btn(BTN_MARK,0);
  CHECK(btn(BTN_BPM,1) && !mute_st.open && last.id==BTN_BPM, "another button (TIME/BPM): menu closes, Roland gets the button");
  open_menu(26); btn(BTN_MARK,0);
  mock_shift=1; CHECK(!btn(BTN_MARK,1) && !mute_st.open, "SHIFT + MARK in the menu: closes it"); mock_shift=0;
  btn(BTN_MARK,0);

  open_menu(26); btn(BTN_MARK,0);
  { int r=0; chr_page_enter(0, mock_pg); r=mute_st.open; CHECK(!r, "play page entered again: menu closed"); }
  CHECK(mute_st.g[26]==g70, "the group set stays after closing");

  /* opening and closing before the release of the opening MARK */
  open_menu(26); CHECK(!btn(BTN_MARK,1) && !mute_st.open, "MARK pressed again before releasing it: closes");
  CHECK(!btn(BTN_MARK,0) && btn(BTN_MARK,0), "  one release taken, the next one Roland's");

  /* the release owed to the menu never reaches the play page (MARK still held while leaving it) */
  open_menu(26); btn(BTN_MARK,0); CHECK(!btn(BTN_MARK,1) && mute_st.eat, "closed by MARK, its release still owed");
  /* ... the release goes to another page; back on the play page, a normal MARK press and release: */
  CHECK(btn(BTN_MARK,1) && btn(BTN_MARK,0), "a later MARK press and its release both reach Roland (MARK not stuck)");
  open_menu(26); CHECK(mute_st.eat, "opened, its release still owed");
  chr_page_enter(0, mock_pg);
  CHECK(!mute_st.eat && btn(BTN_MARK,0), "play page entered again: no release owed any more, the next one is Roland's");

  /* ---- choke */
  memset(mute_st.g,0,sizeof mute_st.g); memset(playing,0,sizeof playing); memset(stopped,0,sizeof stopped);
  mute_st.g[5]=2; mute_st.g[64]=2; mute_st.g[100]=2; mute_st.g[7]=3; mute_st.g[9]=0;
  playing[5]=playing[64]=playing[7]=playing[9]=1;
  stops_total=0; chr_play(K_PANEL, 100, 100);
  CHECK(started==100 && playing[100], "a pad of group 2 starts");
  CHECK(stopped[5]==1 && stopped[64]==1, "  the other playing pads of group 2 stop, in any bank");
  CHECK(!stopped[7] && !stopped[9] && playing[7] && playing[9], "  pads of other groups and pads with no group keep playing");
  CHECK(!stopped[100], "  the pad itself isn't stopped by the choke");
  CHECK(stops_total==2, "  only playing pads are stopped (2 stops)");
  stops_total=0; chr_play(K_PANEL, 9, 100);
  CHECK(stops_total==0, "a pad with no group stops nothing");
  playing[5]=1; stops_total=0; chr_play(2, 64, 100);
  CHECK(stopped[5]==2 && stopped[100]==1 && stops_total==2, "pattern playback (kind 2): chokes too");
  stops_total=0; playing[100]=0; playing[5]=0; chr_play(K_MIDI, 5, 100);
  CHECK(stops_total==1 && stopped[64]==2, "MIDI (kind 3): chokes too, only what's playing");

  /* the menu sets what the choke uses */
  memset(mute_st.g,0,sizeof mute_st.g); memset(playing,0,sizeof playing); memset(stopped,0,sizeof stopped);
  open_menu(11); btn(BTN_MARK,0); knob(0, 127); btn(BTN_MARK,1); btn(BTN_MARK,0);   /* pad 11: group 10 */
  open_menu(23); btn(BTN_MARK,0); knob(0, 127); btn(BTN_MARK,1); btn(BTN_MARK,0);   /* pad 23: group 10 */
  playing[11]=1; chr_play(K_PANEL, 23, 100);
  CHECK(stopped[11]==1, "groups set in the menu choke each other");

  /* ---- saving (MUTEGRP.BIN) */
  memset(playing,0,sizeof playing); memset(mute_st.g,0,sizeof mute_st.g); memset(padinfo,0,sizeof padinfo);
  setfp(10,100000); setfp(20,200000); setfp(30,300000); setfp(40,400000); setfp(60,600000);
  vari_need_load=1; ticks(1);
  int none=1; for(int p=0;p<120;p++) if(mute_st.g[p]) none=0;
  CHECK(none && mute_st.magic==MUTE_MAGIC && file_of(MPATH)<0, "boot with no MUTEGRP.BIN: no groups, nothing written");
  set_group(10,20); set_group(20,127); set_group(30,70); int g30=mute_st.g[30];
  CHECK(mute_st.g[10]==1 && mute_st.g[20]==10 && g30>=5, "groups set in the menu");
  int fw=fwrites; ticks(5);
  CHECK(fwrites==fw, "not written while still changing (within a second)");
  ticks(8); int f=file_of(MPATH);
  CHECK(f>=0 && flen[f]==244, "written about a second after the change: 244 bytes");
  if(f>=0){
    unsigned m; memcpy(&m,fdata[f],4);
    CHECK(m==MUTE_MAGIC && fdata[f][4+10]==1 && fdata[f][4+20]==10 && fdata[f][4+30]==g30 && fdata[f][4+11]==0,
          "  file: MUT1, then each pad's group");
    CHECK(fdata[f][124+10]==pad_fp(10) && fdata[f][124+30]==pad_fp(30) && fdata[f][124+11]==0,
          "  then each pad's sample fingerprint");
  }
  fw=fwrites; ticks(20); CHECK(fwrites==fw, "written once: nothing more to write");

  memset(mute_st.g,0,sizeof mute_st.g); vari_need_load=1; ticks(1);
  CHECK(mute_st.g[10]==1 && mute_st.g[20]==10 && mute_st.g[30]==g30 && mute_st.g[11]==0, "reloaded from the card");
  fw=fwrites; ticks(15); CHECK(fwrites==fw, "  loading doesn't write");
  setfp(30,333333); vari_need_load=1; ticks(1);
  CHECK(mute_st.g[30]==0 && mute_st.g[10]==1, "a pad whose sample changed while away: no group after loading");

  setfp(10,0); ticks(1);
  CHECK(mute_st.g[10]==0 && mute_st.g[20]==10, "sample deleted: its pad loses the group, the others keep theirs");
  setfp(20,222222); ticks(1);
  CHECK(mute_st.g[20]==0, "pad recorded over / sample imported: loses its group");
  ticks(12); f=file_of(MPATH);
  CHECK(f>=0 && fdata[f][4+10]==0 && fdata[f][4+20]==0, "  and the card follows");

  /* copy / exchange: the group goes with the sample */
  set_group(40,50); int g40=mute_st.g[40]; set_group(60,127);   /* pad 40: group 4, pad 60: group 10 */
  CHECK(g40==4 && mute_st.g[60]==10, "copy / exchange setup: pad 40 group 4, pad 60 group 10");
  prm[60]=40; prm[61]=50; job_src=40; job_dst=50;
  CHECK(!(mute_xop(job_copy,0)&0xFF) && mute_st.g[50]==4 && mute_st.g[40]==4, "copy 40 -> 50: the copy gets the group, the source keeps it");
  ticks(3); CHECK(mute_st.g[50]==4, "  and keeps it on the next ticks (same sample as its fingerprint)");
  prm[60]=40; prm[61]=60; job_src=40; job_dst=60;
  mute_xop(job_exch,1);
  CHECK(mute_st.g[40]==10 && mute_st.g[60]==4, "exchange 40 <-> 60: the groups swap");
  ticks(3); CHECK(mute_st.g[40]==10 && mute_st.g[60]==4, "  and stay swapped");
  set_group(50,127);                                      /* pad 50 (a copy of 40's old sample): group 10 */
  prm[60]=60; prm[61]=50; job_src=60; job_dst=50;         /* 60 and 50 hold the same sample */
  mute_xop(job_exch,1);
  CHECK(mute_st.g[60]==10 && mute_st.g[50]==4, "exchange of two identical samples: groups swap too");
  prm[60]=40; prm[61]=50; job_src=40; job_dst=50; int before50=mute_st.g[50];
  CHECK((mute_xop(job_fail,0)&0xFF) && mute_st.g[50]==before50, "a copy that fails: nothing moves");
  prm[60]=200; prm[61]=50; job_calls=0;
  mute_xop(job_noop,0); CHECK(job_calls==1 && mute_st.g[50]==before50, "invalid pads: Roland's job runs, groups untouched");
  /* the 100 ms tick runs while the job has half done PAD_INFO */
  set_group(40,50); setfp(70,700000); ticks(1); set_group(70,127);    /* pad 70: another sample, group 10 */
  prm[60]=40; prm[61]=70; job_src=40; job_dst=70;
  mute_xop(job_copy_tick,0);
  CHECK(mute_st.g[70]==4 && mute_st.g[40]==4, "a tick in the middle of the copy: the copy still gets the group");
  ticks(3); CHECK(mute_st.g[70]==4, "  and keeps it");

  /* invalid files */
  f=file_of(MPATH);
  ticks(15); memcpy(fdata[f],"XXXX",4); vari_need_load=1; ticks(1);
  none=1; for(int p=0;p<120;p++) if(mute_st.g[p]) none=0;
  CHECK(none, "a file with another magic: no groups");
  set_group(40,50); ticks(15); f=file_of(MPATH);
  fdata[f][4+40]=11; vari_need_load=1; ticks(1);
  CHECK(mute_st.g[40]==0, "a group above 10 in the file: none");
  set_group(40,50); ticks(15); f=file_of(MPATH);
  flen[f]=200; vari_need_load=1; ticks(1);
  CHECK(mute_st.g[40]==0, "a short file: no groups");

  printf("%d failures\n", fails);
  return fails != 0;
}
