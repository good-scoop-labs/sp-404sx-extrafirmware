/* Host test: TIME/BPM page logic (display numbers per knob / Time Modify / PTN, knob events,
   CTRL 3 view, CTRL 2 reset). Roland functions are replaced by mocks; vari.c is compiled for the PC. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int prm[64], patt=1200, shown=-999; static const char *shs=0;
static int g(int id,int pad){ if(id==52) return 7; if(pad!=7) abort(); return prm[id]; }
static int dv(int a,int b){return a/b;}
static int pt(void){ return patt*10; }
static void sn(int n){ shown=n; shs=0; } static void ss(const char*s,int f){ shs=s; shown=-999; }
#define R_GET g
#define R_DIV dv
#define R_SHOWN sn
#define R_SHOWS ss
static int setcalls=0, dispcalls=0, evcalls=0;
static void st(int id,int v,int pad,int f){ prm[id]=v; setcalls++; }
struct page; struct event;
static int evo(struct page *p, struct event *e);
static void dsp(struct page *p){ dispcalls++; }
#define R_SET st
#define R_EVORIG evo
#define R_DISP dsp
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
static void lk(int n){} static int cr(const char*p){return -1;} static int op(const char*p,int m){return -1;} static int rd(int f,void*b,int n){return 0;} static int wr(int f,const void*b,int n){return n;} static void cl(int f){} static int cardok(void){return 1;}
#define R_LOCK lk
#define R_UNLOCK lk
#define R_CREATE cr
#define R_OPEN op
#define R_READ rd
#define R_WRITE wr
#define R_CLOSE cl
#define R_CARDOK cardok
#include "vari.c"
#include "ratio.c"
static int stock_mod_reset=1;
static int evo(struct page *p, struct event *e){   /* stock CTRL2: 0 = halve, 127 = double, then mod = new orig */
  evcalls++; if(e->id!=1){ return 0; } p->mode=6;
  int o=prm[42]; int no = e->val<=2? o/2 : e->val>=125? o*2 : o; prm[42]=no;
  if(stock_mod_reset) prm[44]=dv(no+5,10)*10; return 0; }
#define pg page
static int fails=0;
static void expect(int mode, int f12, int want_ret, int want_num, const char *want_str, const char *what){
  struct page p={0,mode,0,f12}; shown=-999; shs=0;
  int r=vari_display((struct page*)&p);
  int ok = r==want_ret && (r==0 || (want_str? shs==want_str : shown==want_num));
  const char *nm = !shs?"-": shs==STR_OFF?"OFF": shs==STR_PTN?"PTN": shs==STR_DASH?"---":"?";
  printf("%s: %s (ret %d num %d str %s)\n", ok?"ok  ":"FAIL", what, r, shown, nm);
  if(!ok) fails++;
}
static void setknob(int v){ vari_tab.e[7].pos=(s8)(v-64); }
int main(){
  prm[42]=1200; prm[43]=0; prm[44]=1000; prm[40]=0; patt=900;
  setknob(64);
  expect(8,0,1,120,0,"normal speed: CTRL3 shows the sample BPM");
  expect(6,0,0,0,0,"normal speed: CTRL2 stock");
  expect(7,0,0,0,0,"normal speed: CTRL1 stock");
  expect(5,0,0,0,0,"other modes untouched");
  setknob(3);                                   /* -12 semitones = 0.5x */
  expect(8,0,1,60,0,"-12 st: CTRL3 shows 60");
  expect(6,0,1,60,0,"-12 st: CTRL2 shows 120 x 0.5 = 60");
  expect(6,1,0,0,0,"-12 st: CTRL2 showing the pattern tempo stays stock");
  expect(7,0,0,0,0,"-12 st, Time Modify OFF: CTRL1 stock (OFF)");
  prm[43]=2;                                    /* Time Modify user BPM 100 */
  expect(8,0,1,50,0,"-12 st + TM 100: CTRL3 shows 50");
  expect(7,0,1,50,0,"-12 st + TM 100: CTRL1 shows 100 x 0.5 = 50");
  expect(6,0,1,60,0,"-12 st + TM 100: CTRL2 shows 120 x 0.5 = 60");
  prm[40]=1;                                    /* Time Modify not available */
  expect(8,0,1,60,0,"TM unavailable: CTRL3 uses the original BPM");
  expect(7,0,0,0,0,"TM unavailable: CTRL1 stock (---)");
  prm[40]=0; prm[43]=1;                         /* Time Modify PTN */
  expect(8,0,1,45,0,"TM PTN (pattern 90) at -12 st: CTRL3 shows 45");
  expect(7,0,0,0,0,"TM PTN: CTRL1 stock (PTN)");
  setknob(1);
  expect(8,0,1,0,STR_OFF,"knob fully left: CTRL3 shows OFF");
  expect(6,0,0,0,0,"OFF: CTRL2 stock");
  setknob(127); prm[43]=0;
  expect(8,0,1,0,STR_PTN,"knob fully right: CTRL3 shows PTN");
  expect(6,0,1,90,0,"PTN, no TM: CTRL2 shows 120 x (90/120) = 90");
  printf("%s: PTN ratio 0.75 (%x)\n", pad_ratio(7)==0xC000?"ok  ":"FAIL", pad_ratio(7)); if(pad_ratio(7)!=0xC000) fails++;
  prm[43]=2;
  expect(7,0,1,90,0,"PTN + TM 100: CTRL1 shows 90 (the pattern tempo)");
  expect(6,0,1,108,0,"PTN + TM 100: CTRL2 shows 120 x 0.9 = 108");
  prm[43]=1;
  printf("%s: PTN + TM PTN = normal speed\n", pad_ratio(7)==UNITY?"ok  ":"FAIL"); if(pad_ratio(7)!=UNITY) fails++;
  expect(6,0,0,0,0,"PTN + TM PTN: CTRL2 stock");
  prm[43]=0; prm[42]=400; patt=2000;
  printf("%s: clamp high %x\n", pad_ratio(7)==R_MAX?"ok  ":"FAIL", pad_ratio(7)); if(pad_ratio(7)!=R_MAX) fails++;
  patt=4000; printf("%s: tempo beyond 300 BPM ignored\n", pad_ratio(7)==UNITY?"ok  ":"FAIL"); if(pad_ratio(7)!=UNITY) fails++;
  prm[42]=1200;
  patt=200;
  printf("%s: clamp low %x\n", pad_ratio(7)==R_MIN?"ok  ":"FAIL", pad_ratio(7)); if(pad_ratio(7)!=R_MIN) fails++;
  patt=0;
  printf("%s: no pattern tempo -> normal\n", pad_ratio(7)==UNITY?"ok  ":"FAIL"); if(pad_ratio(7)!=UNITY) fails++;
  prm[42]=0; patt=900;
  printf("%s: no sample BPM -> normal\n", pad_ratio(7)==UNITY?"ok  ":"FAIL"); if(pad_ratio(7)!=UNITY) fails++;
  prm[42]=1200;
  /* knob events */
  { struct pg p={0,7,0,0}; struct event e={1,2,127}; vari_dirty_ms=0; setknob(64);
    int r=vari_event((struct page*)&p,&e);
    int ok=r==1 && p.mode==8 && vari_tab.e[7].pos>=PTN_POS && shs==STR_PTN && vari_dirty_ms==1;
    printf("%s: CTRL3 to 127 -> PTN, saved later\n",ok?"ok  ":"FAIL"); fails+=!ok;
    e.val=124; r=vari_event((struct page*)&p,&e);
    ok = pad_ratio(7)==0x17F00 && shown==dv((int)((1200u*0x17F00)>>16)+5,10);
    printf("%s: CTRL3 at 124 -> +7 st, shows %d\n",ok?"ok  ":"FAIL",shown); fails+=!ok;
    e.id=0; r=vari_event((struct page*)&p,&e);
    ok = r==0 && p.mode==7; printf("%s: CTRL1 turn leaves CTRL3 mode\n",ok?"ok  ":"FAIL"); fails+=!ok; }
  /* Time Modify PTN folds by octaves like stock playback */
  vari_tab.e[7].pos=0; prm[43]=1; prm[40]=0; prm[42]=600; patt=1400;
  expect(8,0,1,70,0,"TM PTN, 60 BPM sample, 140 pattern: stock plays half-time 70");
  prm[43]=0; vari_tab.e[7].pos=63; prm[42]=1015; patt=1234;
  { u32 r=pad_ratio(7); double want=123.4/101.5*65536; int ok = r>want-2 && r<want+2;
    printf("%s: PTN uses pattern tempo with 0.01 BPM precision (%x)\n",ok?"ok  ":"FAIL",r); fails+=!ok; }
  prm[43]=2; prm[44]=1100; prm[42]=1000; patt=1300;
  { u32 r=pad_ratio(7); double want=1300.0/1100*65536; int ok = r>want-2 && r<want+2;
    printf("%s: PTN over TM 110 -> pattern 130 (%x)\n",ok?"ok  ":"FAIL",r); fails+=!ok; }
  /* wider PTN range */
  vari_tab.e[7].pos=63; prm[43]=0; prm[42]=800; patt=2000;
  printf("%s: PTN 80 -> 200 BPM = 2.5x (%x)\n", pad_ratio(7)==0x28000?"ok  ":"FAIL", pad_ratio(7)); if(pad_ratio(7)!=0x28000) fails++;
  prm[42]=1800; patt=600;
  printf("%s: PTN 180 -> 60 BPM = 1/3x (%x)\n", pad_ratio(7)==0x5555?"ok  ":"FAIL", pad_ratio(7)); if(pad_ratio(7)!=0x5555) fails++;
  /* CTRL 2 resets varispeed (Roland resets Time Modify itself) */
  { struct page p={0,6,0,0}; struct event e={1,1,100};
    vari_tab.e[7].pos=-30; vari_dirty_ms=0; evcalls=0;
    int r=vari_event(&p,&e);
    int ok = r==0 && vari_tab.e[7].pos==0 && vari_dirty_ms==1 && pad_ratio(7)==UNITY;
    printf("%s: CTRL2 resets speed to normal and saves, stock handler runs\n",ok?"ok  ":"FAIL"); fails+=!ok;
    vari_tab.e[7].pos=63; p.mode=8; r=vari_event(&p,&e);
    ok = r==0 && vari_tab.e[7].pos==0 && p.mode==7; printf("%s: CTRL2 resets PTN; CTRL3 mode handed back as 7\n",ok?"ok  ":"FAIL"); fails+=!ok;
    vari_tab.e[7].pos=-63; p.mode=6; r=vari_event(&p,&e);
    ok = vari_tab.e[7].pos==0; printf("%s: CTRL2 resets OFF to normal (same sound)\n",ok?"ok  ":"FAIL"); fails+=!ok;
    vari_tab.e[7].pos=-30; p.f12=1; r=vari_event(&p,&e);
    ok = r==0 && vari_tab.e[7].pos==-30; printf("%s: CTRL2 on the pattern-tempo page leaves the pad alone\n",ok?"ok  ":"FAIL"); fails+=!ok;
    p.f12=0; e.id=0; r=vari_event(&p,&e);
    ok = vari_tab.e[7].pos==-30; printf("%s: CTRL1 leaves varispeed alone\n",ok?"ok  ":"FAIL"); fails+=!ok; }
  /* CTRL 3 view stays until CTRL 1 / CTRL 2 really move or the TIME/BPM button is pressed */
  { struct page p={0,7,0,0}; struct event e={1,2,90};
    knobs[0]=1; knobs[1]=70; prm[42]=1200; prm[43]=0; prm[40]=0; vari_tab.e[7].pos=0;
    int r=vari_event(&p,&e);
    int ok = r==1 && p.mode==8 && vari_snap[0]==1 && vari_snap[1]==70;
    printf("%s: CTRL3 shows its view and remembers CTRL1/CTRL2 positions\n",ok?"ok  ":"FAIL"); fails+=!ok;
    struct event b={0,5,1}; r=vari_event(&p,&b);
    ok = r==0 && p.mode==8; printf("%s: other button event: mode 8 kept, Roland still handles it\n",ok?"ok  ":"FAIL"); fails+=!ok;
    struct event t={2,0,0}; r=vari_event(&p,&t);
    ok = r==0 && p.mode==8; printf("%s: non-knob/button event: mode 8 kept\n",ok?"ok  ":"FAIL"); fails+=!ok;
    struct event k1={1,0,0}; r=vari_event(&p,&k1);
    ok = r==1 && p.mode==8; printf("%s: CTRL1 wobble 1->0 ignored, CTRL3 view stays\n",ok?"ok  ":"FAIL"); fails+=!ok;
    struct event k2={1,1,72}; int pos0=vari_tab.e[7].pos; r=vari_event(&p,&k2);
    ok = r==1 && p.mode==8 && vari_tab.e[7].pos==pos0; printf("%s: CTRL2 wobble 70->72 ignored, speed kept\n",ok?"ok  ":"FAIL"); fails+=!ok;
    struct event k3={1,0,4}; r=vari_event(&p,&k3);
    ok = r==0 && p.mode==7; printf("%s: CTRL1 really turned (1->4): CTRL1 view (mode 7) to Roland\n",ok?"ok  ":"FAIL"); fails+=!ok;
    e.val=90; vari_event(&p,&e);
    struct event k4={1,1,60}; r=vari_event(&p,&k4);
    ok = r==0 && p.mode==7 && vari_tab.e[7].pos==0; printf("%s: CTRL2 really turned: speed reset, mode 7 to Roland\n",ok?"ok  ":"FAIL"); fails+=!ok;
    e.val=90; vari_event(&p,&e);
    struct event bb={0,40,1}; r=vari_event(&p,&bb);
    ok = r==0 && p.mode==7; printf("%s: TIME/BPM button (40): mode 7 for Roland's handler\n",ok?"ok  ":"FAIL"); fails+=!ok;
    p.mode=6; struct event k5={1,1,71}; vari_tab.e[7].pos=10; r=vari_event(&p,&k5);
    ok = r==0 && p.mode==6 && vari_tab.e[7].pos==0; printf("%s: CTRL2 in its own view: stock-like reset path unchanged\n",ok?"ok  ":"FAIL"); fails+=!ok;
    p.mode=3; struct event k6={1,2,50}; vari_tab.e[7].pos=10; r=vari_event(&p,&k6);
    ok = r==0 && p.mode==3 && vari_tab.e[7].pos==10; printf("%s: START/END sub-page (mode 1..4): CTRL3 left to Roland\n",ok?"ok  ":"FAIL"); fails+=!ok; }
  /* every stored byte gives a safe ratio */
  for(int v=-128;v<128;v++){ vari_tab.e[7].pos=(s8)v; for(int pp=0;pp<5;pp++){ int ps[]={0,1,900,3000,0x7FFF}; patt=ps[pp];
    u32 r=pad_ratio(7); if(r<R_MIN||r>R_MAX){fails++;printf("FAIL ratio %d\n",v);} } }
  printf(fails?"%d FAILED\n":"all ok\n",fails); return fails!=0;
}
