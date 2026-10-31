/* Entry stubs (in the free 1 KB at 0x0C080400). Roland's ABI keeps PR, MACH, MACL across calls;
   GCC treats MACH/MACL as scratch, so every entry from Roland code saves them. */
        .section .entry,"ax"
        .global _start
_start:
/* ---- TIME/BPM display hook: trampoline at 0x0C0CB5D4 jumps here with r4 = page */
disp_stub:
        sts.l   pr,@-r15
        sts.l   mach,@-r15
        sts.l   macl,@-r15
        mov.l   r4,@-r15
        mov.l   .Lc_disp,r0
        jsr     @r0
        nop
        mov.l   @r15+,r4
        lds.l   @r15+,macl
        lds.l   @r15+,mach
        lds.l   @r15+,pr
        tst     r0,r0
        bt      1f
        rts
        nop
1:      sts.l   pr,@-r15          /* replay the 6 replaced instructions, continue at 0x0C0CB5E0 */
        mov.l   r10,@-r15
        mov.l   r9,@-r15
        mov     r4,r10
        mov.l   r8,@-r15
        mov     #7,r4
        mov.l   .Ldisp_back,r3
        jmp     @r3
        nop

/* ---- TIME/BPM event hook: CPageSmplEdit vtable entry, r4 = page, r5 = event */
ev_stub:
        sts.l   pr,@-r15
        sts.l   mach,@-r15
        sts.l   macl,@-r15
        mov.l   r4,@-r15
        mov.l   r5,@-r15
        mov.l   .Lc_ev,r0
        jsr     @r0
        nop
        mov.l   @r15+,r5
        mov.l   @r15+,r4
        lds.l   @r15+,macl
        lds.l   @r15+,mach
        lds.l   @r15+,pr
        tst     r0,r0
        bt      2f
        rts
        nop
2:      mov.l   .Lev_orig,r0
        jmp     @r0
        nop

/* ---- sample feeder fill(obj r4, L r5, R r6, flag r7, frames* @r15): forward and reverse classes.
   Calls vari_fill(obj, L, R, flag, frames, original_fill). */
fill_fwd:
        mov.l   .Lfill_fwd_orig,r0
        bra     3f
        nop
fill_rev:
        mov.l   .Lfill_rev_orig,r0
3:      mov.l   @r15,r1           /* 5th arg: frames* */
        sts.l   pr,@-r15
        sts.l   mach,@-r15
        sts.l   macl,@-r15
        mov.l   r0,@-r15          /* 6th arg for C: original fill */
        mov.l   r1,@-r15          /* 5th arg for C: frames*       */
        mov.l   .Lc_fill,r0
        jsr     @r0
        nop
        add     #8,r15
        lds.l   @r15+,macl
        lds.l   @r15+,mach
        lds.l   @r15+,pr
        rts
        nop

/* ---- replaces the hidden PAD_INFO debug dump (its code space is reused): report success */
dump_ok:
        rts
        mov     #1,r0

        .balign 4
.Lc_disp:        .long vari_display
.Lc_ev:          .long vari_event
.Lc_fill:        .long vari_fill
.Ldisp_back:     .long 0x0C0CB5E0
.Lev_orig:       .long 0x0C0CC078
.Lfill_fwd_orig: .long 0x0C0914B8
.Lfill_rev_orig: .long 0x0C0921FC
        .global disp_stub, ev_stub, fill_fwd, fill_rev, dump_ok, stub_lits_end
stub_lits_end:

/* ---- persistence hooks (placed in the reclaimed unused OS routine) ---- */
        .section .text.cavestubs,"ax"
        .balign 2
/* replaces the pointer to Roland's PAD_INFO load 0x0C08A68C (no arguments, returns 1 on success) */
load_stub:                       /* run Roland's load, THEN flag a reload for the next tick */
        sts.l   pr,@-r15
        mov.l   .Lload_orig,r0
        jsr     @r0
        nop
        mov.l   .Lneed_load,r1
        mov     #1,r2
        mov.l   r2,@r1
        lds.l   @r15+,pr
        rts
        nop
/* replaces the pointer to Roland's 100 ms PAD_INFO auto-save 0x0C08A700 (r4 = elapsed ms) */
tick_stub:
        sts.l   pr,@-r15
        sts.l   mach,@-r15
        sts.l   macl,@-r15
        mov.l   r4,@-r15
        mov.l   .Ltick_orig,r0
        jsr     @r0
        nop
        mov.l   @r15+,r5
        mov     r0,r4
        mov.l   .Lc_tick,r0
        jsr     @r0
        nop
        lds.l   @r15+,macl
        lds.l   @r15+,mach
        lds.l   @r15+,pr
        rts
        nop
        .balign 4
.Lload_orig:    .long 0x0C08A68C
.Lneed_load:    .long vari_need_load
.Ltick_orig:    .long 0x0C08A700
.Lc_tick:       .long vari_tick
        .global load_stub, tick_stub, persist_lits_end
persist_lits_end:
