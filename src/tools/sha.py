"""Tiny SH-2A assembler: just the instructions our patches use. Every helper returns big-endian bytes.
Results are re-checked by disassembling with capstone in the build scripts."""
import struct

def _w(x): return struct.pack('>H', x & 0xFFFF)
def r(n): assert 0 <= n <= 15; return n

def mov_imm(imm, n):        # mov #imm,Rn  (imm sign-extended 8-bit)
    assert -128 <= imm <= 127; return _w(0xE000 | r(n) << 8 | (imm & 0xFF))
def add_imm(imm, n):        # add #imm,Rn
    assert -128 <= imm <= 127; return _w(0x7000 | r(n) << 8 | (imm & 0xFF))
def movb_store(m, n):       # mov.b Rm,@Rn
    return _w(0x2000 | r(n) << 8 | r(m) << 4)
def mov(m, n):              # mov Rm,Rn
    return _w(0x6003 | r(n) << 8 | r(m) << 4)
def movl_pc(pc, target, n): # mov.l @(disp,PC),Rn  loading the long at `target`
    ea_base = (pc & ~3) + 4; d = target - ea_base
    assert d >= 0 and d % 4 == 0 and d // 4 <= 255 and target % 4 == 0, (hex(pc), hex(target))
    return _w(0xD000 | r(n) << 8 | d // 4)
def jmp(m):  return _w(0x402B | r(m) << 8)
def rts():   return _w(0x000B)
def nop():   return _w(0x0009)
def long(v): return struct.pack('>I', v)
