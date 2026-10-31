/* Host test: the varispeed resampler (vari_fill) against a whole-stream reference, bit-exact,
   over random block sizes, speeds and PTN ratios (0.25x .. 4x). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int prm[64], patt=1200; static int g(int id,int pad){return id==52?7:prm[id];} static int dv(int a,int b){return a/b;}
static int pt(void){ return patt*10; }
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
/* fake Roland feeder object */
struct obj { char pad0[16]; int cursor; char pad1[4]; int voice; int pad; char rest[64]; };
static s16 *srcL,*srcR; static int srcN;
static int ofill(void *o_, s16 *L, s16 *R, int *flag, int *frames){
  struct obj *o=o_; int n=*frames, i;
  for(i=0;i<n;i++){ if(o->cursor>=srcN) break; L[i]=srcL[o->cursor]; R[i]=srcR[o->cursor]; o->cursor++; }
  if(i<n){ for(int j=i;j<n;j++){L[j]=0;R[j]=0;} *frames=i; return 0; }
  return 1;
}
/* reference: whole-stream resampler with the same arithmetic */
static int ref(s16 *outL,s16 *outR,int maxout,u32 r,int *validout){
  u32 ph=2*UNITY; int aL=0,aR=0,bL=0,bR=0,k=0,j; *validout=-1;
  for(j=0;j<maxout;j++){
    while(ph>=UNITY){ph-=UNITY;aL=bL;aR=bR; bL=k<srcN?srcL[k]:0; bR=k<srcN?srcR[k]:0; k++;}
    if(k>srcN && *validout<0) *validout=j;
    outL[j]=lerp(aL,bL,ph); outR[j]=lerp(aR,bR,ph); ph+=r;
  }
  return j;
}
int main(){
  srand(12345); int fails=0, cases=0;
  int positions[]={-64,-50,-20,-3,-2,1,2,10,40,60,61,61,61,63,61,61,61,61,61};
  int pats[]={0,0,0,0,0,0,0,0,0,0,900,1800,1200,450,300,360,3000,4700,9000};
  for(int pi=0;pi<19;pi++) for(int trial=0;trial<40;trial++){
    srcN = 500 + rand()%20000;
    srcL=malloc(srcN*2); srcR=malloc(srcN*2);
    for(int i=0;i<srcN;i++){srcL[i]=(s16)(rand()-RAND_MAX/2); srcR[i]=(s16)(rand()-RAND_MAX/2);}
    struct obj o; memset(&o,0,sizeof o); o.voice=3; o.pad=7; memset(vari_vs,0,sizeof vari_vs);
    vari_tab.e[7].pos=positions[pi]; prm[42]=1200; prm[43]=0; patt=pats[pi]?pats[pi]:1200;
    u32 r=pad_ratio(7);
    int total=0, maxout=srcN*3+50; s16 *gotL=calloc(maxout+8192,2), *gotR=calloc(maxout+8192,2);
    int endedat=-1, flag=0;
    while(total<maxout){
      int F = (rand()%3==0)? 1+rand()%8 : (rand()%2? 4096 : 1+rand()%5000);
      s16 *L=calloc(F,2),*R=calloc(F,2);
      for(int i=0;i<F;i++){L[i]=0x5555;R[i]=0x2222;}   /* garbage */
      int fr=F; int rv=vari_fill(&o,L,R,&flag,&fr,ofill);
      memcpy(gotL+total,L,F*2); memcpy(gotR+total,R,F*2);
      if(rv==0){ endedat = total+fr; total+=F; free(L);free(R); break; }
      total+=F; free(L);free(R);
    }
    s16 *eL=calloc(maxout+8192,2),*eR=calloc(maxout+8192,2); int vo;
    ref(eL,eR,total,r,&vo);
    int bad=0; for(int j=0;j<total;j++) if(gotL[j]!=eL[j]||gotR[j]!=eR[j]){ if(!bad) printf("pos %d trial %d: mismatch at %d of %d (got %d exp %d)\n",positions[pi],trial,j,total,gotL[j],eL[j]); bad=1; }
    int expect_end = (r==UNITY)? srcN : vo;
    if(endedat!=expect_end){ printf("pos %d trial %d: end at %d, expected %d (srcN %d)\n",positions[pi],trial,endedat,expect_end,srcN); bad=1; }
    fails+=bad; cases++;
    free(srcL);free(srcR);free(gotL);free(gotR);free(eL);free(eR);
  }
  printf("%d cases, %d failures\n",cases,fails); return fails!=0;
}
