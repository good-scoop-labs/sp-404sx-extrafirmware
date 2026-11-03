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
.Lc_ev:          .long vari_ev
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
.Lc_tick:       .long chr_tick
        .global load_stub, tick_stub, persist_lits_end
persist_lits_end:

/* ---- chromatic mode hooks (placed in reclaimed factory test page methods). Arguments pass through unchanged:
   play request (req, kind, pad, vel), pad start (kind, pad, vel), pad release (pad). */
        .section .text.chrstubs,"ax"
        .balign 2
req_stub:
        mov.l   .Lc_req,r0
        bra     5f
        nop
play_stub:
        mov.l   .Lc_play,r0
        bra     5f
        nop
rel_stub:
        mov.l   .Lc_rel,r0
5:      sts.l   pr,@-r15
        sts.l   mach,@-r15
        sts.l   macl,@-r15
        jsr     @r0
        nop
        lds.l   @r15+,macl
        lds.l   @r15+,mach
        lds.l   @r15+,pr
        rts
        nop
        .balign 4
chr_lits:
.Lc_req:        .long chr_request
.Lc_play:       .long chr_play
.Lc_rel:        .long chr_release
chr_lits_end:

/* ---- page and note hooks: Roland's original function is passed as the next argument: play page event (page,
   event, handler), play page enter / refresh (page, method), MIDI note handler (event, handler), MIDI receive
   gate (event, gate). */
        .section .text.chrstubs2,"ax"
        .balign 2
pev_stub:                        /* play page event */
        mov.l   .Lpev_orig,r6
        mov.l   .Lc_pev,r0
        bra     6f
        nop
note_stub:                       /* note handler (event) */
        mov.l   .Lnote_orig,r5
        mov.l   .Lc_note,r0
        bra     6f
        nop
gate_stub:                       /* MIDI receive gate (no arguments; the event is in Roland's r8) */
        mov     r8,r4
        mov.l   .Lgate_orig,r5
        mov.l   .Lc_gate,r0
        bra     6f
        nop
gate2_stub:                      /* note handlers' MIDI gate (no arguments) */
        mov.l   .Lgate_orig,r4
        mov.l   .Lc_gate2,r0
        bra     6f
        nop
hold_stub:                       /* HOLD's GATE lookup: R_GET(id, pad) */
        mov.l   .Lhold_orig,r6
        mov.l   .Lc_hold,r0
        bra     6f
        nop
pen_stub:                        /* play page enter: Roland's enter, then C.ON due */
        mov.l   .Lpen_orig,r5
        mov.l   .Lc_pen,r0
        bra     6f
        nop
xcopy_stub:                      /* pad copy worker (no arguments; pads in params 60 / 61) */
        mov.l   .Lxcopy_orig,r4
        mov     #0,r5
        mov.l   .Lc_xop,r0
        bra     6f
        nop
xexch_stub:                      /* pad exchange worker (no arguments; pads in params 60 / 61) */
        mov.l   .Lxexch_orig,r4
        mov     #1,r5
        mov.l   .Lc_xop,r0
        bra     6f
        nop
pref_stub:                       /* play page refresh: Roland's refresh, then C.ON if due */
        mov.l   .Lpref_orig,r5
        mov.l   .Lc_pref,r0
6:      sts.l   pr,@-r15
        sts.l   mach,@-r15
        sts.l   macl,@-r15
        jsr     @r0
        nop
        lds.l   @r15+,macl
        lds.l   @r15+,mach
        lds.l   @r15+,pr
        rts
        nop
        .balign 4
chr_lits2:
.Lc_pev:        .long chr_page_event
.Lpev_orig:     .long 0x0C0C5384
.Lc_note:       .long chr_note
.Lnote_orig:    .long 0x0C089330
.Lc_gate:       .long chr_gate
.Lgate_orig:    .long 0x0C08FD70
.Lc_gate2:      .long chr_gate2
.Lc_hold:       .long chr_holdget
.Lhold_orig:    .long 0x0C082B34
.Lc_pen:        .long chr_page_enter
.Lpen_orig:     .long 0x0C0C5250
.Lc_pref:       .long chr_page_refresh
.Lpref_orig:    .long 0x0C0C53F4
.Lc_xop:        .long vari_xop
.Lxcopy_orig:   .long 0x0C087A64
.Lxexch_orig:   .long 0x0C087DEC
chr_lits2_end:
        .global req_stub, play_stub, rel_stub, pev_stub, note_stub, gate_stub, gate2_stub, hold_stub, pen_stub, pref_stub
        .global xcopy_stub, xexch_stub
        .global chr_lits, chr_lits_end, chr_lits2, chr_lits2_end
