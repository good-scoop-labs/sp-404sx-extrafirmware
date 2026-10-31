/* Host test: long randomized run of vari_fill with random speeds, tempos and parameters, built with
   AddressSanitizer / UBSan by run_tests.sh. Catches buffer overruns and arithmetic overflow. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int prm[64], patt=1200; static int g(int id,int pad){return id==52?7:prm[id];} static int dv(int a,int b){return a/b;}
static int pt(void){ return patt; }
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
static void sn(int n){} static void ss(const char*s,int f){}
#define R_GET g
#define R_DIV dv
#define R_SHOWN sn
#define R_SHOWS ss
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
static int evo(struct page *p, struct event *e){ evcalls++; return 0; }
struct obj { char pad0[16]; int cursor; char pad1[4]; int voice; int pad; char rest[64]; };
static s16 *srcL,*srcR; static int srcN; static long fetched;
static int ofill(void *o_, s16 *L, s16 *R, int *flag, int *frames){
  struct obj *o=o_; int n=*frames, i; if(n<=0){printf("fill asked for %d\n",n); exit(1);}
  fetched+=n;
  for(i=0;i<n;i++){ if(o->cursor>=srcN) break; L[i]=srcL[o->cursor]; R[i]=srcR[o->cursor]; o->cursor++; }
  if(i<n){ for(int j=i;j<n;j++){L[j]=0;R[j]=0;} *frames=i; return 0; }
  return 1;
}
int main(){
  srand(777); srcN=200000; srcL=malloc(srcN*2); srcR=malloc(srcN*2);
  for(int i=0;i<srcN;i++){srcL[i]=(s16)(rand());srcR[i]=(s16)rand();}
  struct obj o; memset(&o,0,sizeof o); o.voice=5; o.pad=11; int flag=0; long outs=0;
  for(int it=0; it<200000; it++){
    if(rand()%50==0) vari_tab.e[11].pos=(s8)(rand()%2? rand()%128-64 : (rand()%2? 61+rand()%3 : rand()%256-128));
    if(rand()%40==0){ int w[]={0,-5,1,400,1200,2000,3000,40000,0x7FFF,0x8000,0x7FFFFFFF,300,4790,100};
      patt=w[rand()%14]; prm[42]=w[rand()%14]; prm[44]=w[rand()%11]; prm[43]=rand()%4; prm[40]=rand()%3; }
    if(rand()%300==0) o.cursor=rand()%srcN;           /* retrigger / loop jump */
    int F=(rand()%4==0)?1+rand()%6:1+rand()%4096;
    s16 *L=malloc(F*2),*R=malloc(F*2);                 /* exact-size buffers: ASan catches overruns */
    int fr=F; int rv=vari_fill(&o,L,R,&flag,&fr,ofill);
    { u32 rr=pad_ratio(11); if(rr<0x4000||rr>0x3FFFF){printf("ratio out of range %x\n",rr);return 1;} }
    if(fr<0||fr>F){printf("bad frames %d\n",fr);return 1;}
    outs+=F; free(L);free(R);
    if(rv==0) o.cursor=rand()%srcN;
  }
  printf("ok: %ld outputs, %ld source frames fetched\n",outs,fetched); return 0;
}
