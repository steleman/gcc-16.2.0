-mcmodel=large with -fpic/-fPIC for AArch64 and RISCV64 -- GCC 16 patch series
==============================================================================

  0001  aarch64   Core: code model, PC-relative (MOVW_PREL) and GOT (MOVW_GOTOFF)
                  materialization, GOT base computed once per function
  0002  aarch64   TLS: initial-exec via TLSIE_MOVW_GOTTPREL_G1/_G0_NC;
                  general/local-dynamic diagnosed (sorry)
  0003  aarch64   Accept -mcmodel=large with -fpic/-fPIC; invoke.texi
  0004  aarch64   gcc.target/aarch64/large-pic-{1..8}.c
  0005  riscv     Core: self-relative literal pool entries + .data.rel.ro slots
  0006  riscv     Accept -mcmodel=large with -fpic/-fPIC on RV64; invoke.texi
  0007  riscv     gcc.target/riscv/large-pic-{1..4}.c
  0008  riscv     8-byte PC-relative EH pointer encodings (FDE, personality,
                  LSDA) in the large code model, PIC and non-PIC; defaults to
                  -fno-dwarf2-cfi-asm there so the FDE pointers are 8-byte too;
                  gcc.target/riscv/large-model-eh-{1,2}.c
  0009  libgcc    Binary search of 64-bit (DW_EH_PE_datarel|sdata8)
                  .eh_frame_hdr tables, which ld.bfd and lld emit when code is
                  out of 32-bit reach

Base: gcc-16.2.0 (release tarball, with most of Fedora 44's patches, which
touch none of these files).

The patches apply to the pristine 16.2.0 tarball with no offsets or fuzz.
They were first written against gcc-16.0.1-20260321 (Fedora 44 patches)
where each intermediate state (after 0001, 0002 and 0005) was built to
confirm it compiles.  See "Port to gcc-16.2.0" below for what changed.

Companion series:
  Binutils: https://github.com/steleman/binutils-2.46.1
  LLVM:     https://github.com/steleman/llvm-23.1.1

LLVM's RISCV large model put jump tables in .rodata behind a +/-2 GiB auipc
until LLVM commit 98677af3cb50 (series mcmodel-large-jt-riscv), found on
2026-08-17 by the far data layout test (item g below).  GCC keeps large-model
jump tables in the function's section (JUMP_TABLES_IN_TEXT_SECTION for
CM_LARGE in riscv.h, upstream), so GCC objects were never affected.

The LLVM AArch64 series README.MCMODEL-LARGE.md
(https://github.com/steleman/llvm-23.1.1/blob/main/README.MCMODEL-LARGE.md)
has a "GCC and GNU Binutils" section referencing patches 0001-0004 of this
series and Binutils 0001 (bfd/ld), 0002 (gas) and 0003 (gold).

Two compilers and three linkers implement the sequences: the MOVK rules a
specification must pin down, the 24/24 and the 5 GiB results.

The retest passed, and its Compatibility section now notes the one remaining
difference in unwind tables: LLVM's integrated assembler gives large-model
FDE initial locations 8 bytes, while GNU as builds them from .cfi_*
directives with 4 bytes, which covers all GCC output; so only integrated-assembler
FDEs reach code more than 2 GiB from .eh_frame.

This series is applied to gcc-16.2.0.

Port to gcc-16.2.0
------------------

Patches 01-07 and 09 are unchanged apart from hunk line numbers.  Upstream
changes between 16.0.1 and 16.2.0 in the touched files were reviewed for
interactions: RISCV t1 removed from SIBCALL_REGS (the large PIC thunk path
uses t0/t2, not t1), the RISCV stack-protector patterns now use
riscv_output_move, and the new AArch64 narrow_gp_writes pass (it only narrows
sets whose upper 32 bits are known zero, which never holds for the large PIC
UNSPEC/MEM loads).  None required changes; the tests below confirm it.

Patch 08 changed.  Its encoding change alone does not give 8-byte FDE
pointers whenever GCC uses .cfi_* directives, which it does by default with
any assembler that supports them: gas then builds .eh_frame and always
encodes an FDE's initial location as pcrel|sdata4 (R_RISCV_32_PCREL); only the
personality and LSDA pointers became 8-byte.  large-model-eh-{1,2}.c failed
at every optimization level, and with library code 3 GiB from .eh_frame, lld
rejected the link and ld.bfd linked silently but the program called
std::terminate on the first throw.  (The 16.0.1 test compilers evidently did
not use CFI directives, so this was not visible there.)  Patch 08 now
defaults to -fno-dwarf2-cfi-asm for CM_LARGE in riscv_option_override, so GCC
emits .eh_frame itself (CIE augmentation 0x1c); an explicit -fdwarf2-cfi-asm
is honoured and gives the old, 32-bit FDE pointers.


ABI: what the objects contain
-----------------------------

AArch64 uses exactly the sequences and operators of the LLVM 23.1.1 branch in
llvm-project-mcmodel-large (verified: identical relocation sets for the same source):

  GOT base   adr xD, . ; movz/movk tmp, #:prel_g3/g2_nc/g1_nc/g0_nc:
             _GLOBAL_OFFSET_TABLE_+4/+8/+12/+16 ; add xD, xD, tmp
  preempt.   movz/movk #:gotoff_g3/g2_nc/g1_nc/g0_nc:sym ; ldr [gotbase, x]
  local      same as GOT base, against the symbol (addend absorbs offsets)
  TLS IE     movz/movk #:gottprel_g1/g0_nc:sym ; ldr [gotbase, x] ; add tp
  TLS LE     unchanged
  TLS GD/LD  sorry (as LLVM)

  Relocations: R_AARCH64_MOVW_PREL_G0_NC..G3, R_AARCH64_MOVW_GOTOFF_G0_NC,
  G1_NC, G2_NC, G3, R_AARCH64_TLSIE_MOVW_GOTTPREL_G1/_G0_NC, R_AARCH64_PREL64
  (.eh_frame).  _GLOBAL_OFFSET_TABLE_ must be the start of .got, which holds
  for lld and ld.bfd.  Linkers must NOT relax the MOVW GOTTPREL pair IE->LE.

RISCV uses no new relocations and interoperates with LLVM's prototype
(-riscv-large-pic): R_RISCV_ADD64/SUB64 pool entries in .text, R_RISCV_64 in
.data.rel.ro slots.  Not part of the RISCV psABI (which disallows large+PIC).


Testing performed
-----------------

On gcc-16.2.0 (this version of the series), with cross compilers built from
the pristine tarball and from the patched tree, the binutils series, and
qemu-user:

a. New tests (dg-final emulator; riscv.exp runs the torture options, so
   RISCV counts are per option): AArch64 8/8, RISCV 29/29 pass; all fail
   with the baseline.  Pre-existing large-model.c and
   jump-table-large-code-model.c still pass.
b. No regressions, baseline vs patched, identical flags: 4140 compiled
   gcc.target/aarch64 tests and 1616 gcc.c-torture/compile tests (AArch64),
   6539 gcc.target/riscv and 1615 torture tests (RISCV).  Assembly is
   identical except debug-info paths and LTO bytecode, apart from the three
   RISCV -mcmodel=large tests, which now emit .eh_frame directly (patch 08).
c. Stress: gcc.dg + gcc.c-torture/{compile,execute}, -mcmodel=large -fPIC at
   -O2 and -O0 vs baseline -fPIC, 22,240 compilations per architecture: no
   ICEs.  New failures are only the GD/LD TLS sorry (AArch64, 35 files incl.
   profiling tests that use TLS counters) and gcc.dg/pr119493-2.c (RISCV
   musttail), which fails identically with the baseline non-PIC large model.
d. Runtime ABI matrix as in 1. below: AArch64 24/24 (ld.bfd, ld.gold, lld),
   RISCV 16/16 (ld.bfd, lld), GCC and patched clang 23.1.1 objects mixed
   (clang with -fsemantic-interposition; RISCV clang objects via llc
   -riscv-large-pic).  C++ exceptions across a large-PIC DSO and large PIE:
   AArch64 6/6, RISCV 4/4.
e. RISCV far EH layout: library .text 3 GiB above .eh_frame (INSERT linker
   script): exceptions caught with lld and ld.bfd.  With -fdwarf2-cfi-asm
   (the original patch 08 behaviour) lld rejects R_RISCV_32_PCREL and ld.bfd
   output calls std::terminate.
f. Build: no new warnings in the patched target files.
g. RISCV far data layout: freestanding static programs (own _start, no
   libc) with .rodata, .data.rel.ro, .data and .bss 3 GiB above .text,
   exercising preemptible and hidden globals, statics, a large array, FP
   constants, strings, function pointers, a jump table, computed goto and
   writes.  -mcmodel=large -fPIC links and runs (exit 42) with ld.bfd and
   lld at -O2 and -O0 for GCC-only objects and GCC mixed with clang
   -riscv-large-pic objects: 10/10.  -mcmodel=medany -fPIC fails to link
   with both linkers.  (clang-only -O2 also passes; a clang -O0 object
   addressed its jump table with a +/-2 GiB auipc and failed to link, a gap
   in LLVM's RISCV large model, which accounted for the 2 other mixed links;
   fixed by LLVM commit 98677af3cb50, series mcmodel-large-jt-riscv, after
   which all 16 {GCC, clang} x {GCC, clang} links pass.  A switch that keeps
   a jump table: GCC objects 8/8, clang objects 8/8 with that commit and 0/8
   without, both linkers.)  The linker script needs explicit PHDRS, or lld
   extends the read-only segment holding .eh_frame across the gap to
   .rodata, producing a 3 GiB segment qemu cannot load; the writable segment
   page-aligned away from the read-only one, or qemu rejects .bss
   overlapping a non-writable page; and a non-empty read-only segment,
   because ld.bfd otherwise emits an empty PT_LOAD at address 0 that qemu
   cannot map.
h. AArch64 far data layout: the same programs with all data and the GOT
   5 GiB above .text.  -mcmodel=large -fPIC links and runs (exit 42) with
   ld.bfd, ld.gold and lld at -O2 and -O0 for every {GCC, clang} x
   {GCC, clang} object combination: 24/24.  -mcmodel=small -fPIC fails to
   link with all three linkers (ADR_PREL_PG_HI21 out of range).  The objects'
   .text relocations are only MOVW_PREL_G*, MOVW_GOTOFF_G* and CALL26.
i. AArch64, clang textual output through gas: clang objects built with
   "clang -fno-addrsig -S" (containing "movk xN, #:gotoff_g3:sym") and
   assembled by gas.  Runtime ABI matrix 24/24 and the 5 GiB far layout
   24/24, both with ld.bfd, ld.gold and lld.  Against integrated-assembler
   objects from the same source, the instructions differ only in the
   MOVZ/MOVN placeholder under :gottprel_g1: (rewritten by sign at link
   time); the relocations differ only in symbol vs section symbol for a
   function pointer and in .eh_frame, where gas builds FDEs from .cfi_*
   directives with PREL32 initial locations instead of PREL64.
j. libgcc 64-bit .eh_frame_hdr search (patch 09): a -mcmodel=large -fPIC
   shared library with 200 functions whose .text is 3 GiB above
   .eh_frame_hdr (RISCV: also above .eh_frame; AArch64: .eh_frame placed
   with the code, since its gas-built FDEs have 32-bit pointers), linked into
   an executable; each function looks up its FDE with _Unwind_Find_FDE and
   checks the function and that FDEs are distinct.  Both linkers write a 0x3c
   table.  Intact: 200/200 with patched and baseline libgcc_s.  With every
   table entry's FDE redirected to the first entry's: patched libgcc_s fails
   199/200, baseline libgcc_s (linear scan) still succeeds -- proving the
   binary search is used.  Checked for RISCV and AArch64 with ld.bfd and
   lld; the loaded libgcc_s is confirmed from /proc/self/maps.  AArch64 with
   ld.bfd first showed baseline libgcc_s segfaulting: binutils patch 04 then
   encoded eh_frame_ptr as pcrel sdata4 even with .eh_frame 3 GiB from the
   header, so it silently wrapped (0xffffffffc0013970 for 0xc0013970), and
   only patched libgcc, which never follows eh_frame_ptr when a table exists,
   was unaffected.  Patch 04 now writes an 8-byte eh_frame_ptr as lld does,
   and all four linker/target combinations pass.

k. The same tests at -O1 and -O3 (2026-09-18; the scripts take OPTS, OPT,
   STRESS_OPTS and EXTRA_FLAGS overrides).  Stress, 22,240 compilations per
   architecture: no ICEs; new failures only the GD/LD TLS sorry (AArch64,
   34 files) and gcc.dg/pr119493-2.c (RISCV musttail, which fails identically
   with the baseline non-PIC large model at both levels).  Runtime ABI matrix:
   AArch64 24/24 (and 24/24 with clang textual output through gas), RISCV
   16/16; C++ exceptions (GCC objects only) AArch64 6/6, RISCV 4/4; the
   RISCV far EH layout (e. above) and the exception tests with clang-built
   objects (5. in the 16.0.1 testing below) were not repeated.  Far data:
   AArch64 5 GiB 24/24 (integrated assembler and gas), RISCV 3 GiB 16/16
   with the LLVM jump-table fix; jump-table switch: GCC 8/8, clang 8/8 with
   LLVM 98677af3cb50 and 0/8 without; small/medany controls fail to link.
   .eh_frame_hdr test: unchanged at both levels.  New tests: RISCV torture
   runs at -O1 and -O3 -g 12/12; AArch64 with -O1 or -O3 appended 7/8, the
   exception being large-pic-6.c, the deliberate -O0 variant, whose
   never-written static int folds to 0 so the expected local_var reference
   disappears (the remaining code has no ADRP).

On gcc-16.0.1-20260321 (original testing):

Cross compilers (baseline and patched) for aarch64-linux-gnu and
riscv64-linux-gnu, with glibc sysroots, qemu-user for execution.

1. Runtime ABI matrix: shared library (-mcmodel=large -fPIC) + PIE
   (-mcmodel=large -fPIE), with symbol interposition, weak undefined, jump
   tables, computed goto, FP constants, string literals, large offsets, calls
   between DSOs, TLS IE and LE.  Library/executable compiled by
   {GCC, clang} x {GCC, clang}, -O2 and -O0:
     AArch64: ld.bfd, ld.gold, lld  24/24 pass (GCC output assembled by gas;
              ld.bfd and ld.gold need the binutils series).  Also 24/24
              with clang objects built as clang -fno-addrsig -S + gas.
     RISCV:  ld.bfd and lld        16/16 pass
2. Far layout: freestanding static programs with all data and the GOT placed
   5 GiB (AArch64) / 3 GiB (RISCV) away from the code.  Small/medany model
   fails to link (sanity check); large PIC links and runs, GCC and clang,
   -O2 and -O0 (AArch64: ld.bfd, ld.gold and lld; RISCV: ld.bfd and lld).
3. No regressions: every test in gcc.target/{aarch64,riscv} and
   gcc.c-torture/compile compiled with baseline and patched compilers
   (7036 files): assembly identical except for embedded build paths.
4. Stress: gcc.c-torture/{compile,execute} and gcc.dg compiled with
   -mcmodel=large -fPIC at -O2 and -O0 (about 22,000 compilations per
   architecture), compared against the baseline at small -fPIC: no ICEs; the
   only new failures are the intended GD/LD TLS diagnostic (AArch64) and a
   musttail test that fails identically in the existing non-PIC large model
   (RISCV).
5. Exceptions (patch 08): C++ throw from a large-PIC shared library through
   a destructor and a callback into a large-PIE executable, caught there:
   pass with ld.bfd and lld at -O2/-O0, and with clang-built objects.  With
   the library code placed 3 GiB from .eh_frame, lld links and the
   exceptions are caught correctly.  RISCV differential rerun: only files
   using -mcmodel=large changed.
6. New testsuite cases checked with a dg-final emulator (DejaGnu is not
   installed here): all pass with the series and all fail without it.


Implementation notes
--------------------

AArch64 (gcc/config/aarch64/):
- Symbol types SYMBOL_LARGE_PIC_PREL, _GOT and _TLSIE (aarch64-protos.h) are
  chosen in aarch64_classify_symbol and expanded in
  aarch64_load_symref_appropriately.
- The GOT base lives in the pseudo PIC register and is computed once per
  function on the entry edge by aarch64_init_pic_reg (TARGET_INIT_PIC_REG),
  as i386 does for its large PIC model.
- The six-instruction PC-relative sequence is a single insn
  (aarch64_load_large_pic_prel) with a match_scratch, so its addends stay
  valid.  It cannot be emitted after reload; those paths assert
  can_create_pseudo_p ().
- Non-symbolic constants stay in per-function literal pools (reached with
  ADRP to a label in the same section); symbolic constants are never forced
  to memory in this model.

RISCV (gcc/config/riscv/):
- riscv_large_pic_p () gates everything; riscv_cmodel stays CM_LARGE under
  PIC instead of becoming CM_PIC.
- Pool constants are (const (unspec [...] UNSPEC_LARGE_PIC_DISP/SLOT)),
  printed by riscv_output_addr_const_extra.  GCC's constant pool chooses a
  section per entry (riscv_elf_select_rtx_section), which is what lets slots
  go to .data.rel.ro while displacement entries stay with the code.
- ASM_PREFERRED_EH_DATA_FORMAT (riscv.h) uses sdata8 for CM_LARGE, and
  riscv_option_override defaults to -fno-dwarf2-cfi-asm for CM_LARGE, so
  dwarf2out emits .eh_frame itself instead of .cfi_* directives.  Without the
  latter, gas would build the FDEs with 4-byte initial locations.
- AArch64 and x86-64 large models still use .cfi_* directives, so their FDE
  initial locations stay 4-byte (pre-existing upstream behaviour, not changed
  by this series).


Reproducing the tests
---------------------

Out-of-tree cross builds, e.g. for AArch64 (RISCV: --target=riscv64-linux-gnu
--with-arch=rv64gc --with-abi=lp64d):

  $SRC/configure --target=aarch64-linux-gnu --prefix=$PREFIX \
    --with-sysroot=$SYSROOT --with-glibc-version=2.39 \
    --with-as=$BINUTILS/bin/aarch64-linux-gnu-as \
    --with-ld=$BINUTILS/bin/aarch64-linux-gnu-ld \
    --enable-languages=c,c++ --disable-bootstrap --disable-multilib \
    --enable-shared --enable-threads=posix --disable-libsanitizer \
    --disable-libssp --disable-libgomp --disable-libquadmath \
    --disable-libatomic --disable-libitm --disable-nls
  make all-gcc && make all-target-libgcc

Pitfalls met along the way:
- The sysroot must be a real directory tree, not symlinks: GNU ld resolves
  the absolute paths in libc.so linker scripts relative to the sysroot only
  when the script's real path is inside it.  GNU ld itself must be configured
  with --with-sysroot.  Sysroots used: the Xilinx Vitis AArch64 one (glibc
  2.39, copied with lib64 -> usr/lib added) and /opt/riscv64/sysroot
  (glibc 2.29).
- --with-ld hard-codes the linker, so -fuse-ld=lld is silently ignored.  To
  test several linkers with one compiler, point --with-ld at a wrapper script
  that dispatches on an environment variable.
- Put target binutils on PATH before configuring, or the generated gcc/nm
  wrapper is empty and libgcc fails with "No symbols seen".
- Check that configure found working binutils before trusting any result:
  in the GCC build's gcc/config.log, gcc_cv_as_cfi_directive should be yes
  and gcc_cv_ld_ro_rw_mix should be read-write.  A build whose probes failed
  still works but emits .eh_frame as "aw" and probably no .cfi_* directives,
  so GCC writes .eh_frame by hand.  The original 16.0.1 test compilers were
  like that: it produced the wrong "aw" note (corrected under Known
  limitations) and hid the patch 08 bug, which only shows when GNU as builds
  the FDEs.
- DejaGnu is not installed, so the checks above used a differential compile
  harness and a dg-final emulator.  The default torture options add -O2;
  tests skipped at -O0 must be checked with it.
- Pre-existing and unrelated: gcc.target/riscv/pr122051.c ICEs in
  emit_move_multi_word.


Binutils
--------

The binutils series above adds everything this model needs: the gas
operators, ld.bfd and ld.gold relocation support, a 64-bit .eh_frame_hdr
(8-byte eh_frame_ptr and search table, as lld writes; the eh_frame_ptr part
was added on 2026-09-17, after the table-only version truncated it when
.eh_frame itself was more than 2 GiB from the header) and a RISCV ld.bfd fix
for 8-byte .eh_frame fields.  With it, AArch64 output
from GCC is assembled by gas and links and runs with ld.bfd, ld.gold and lld,
mixed freely with clang objects (24/24 combinations), and RISCV exceptions
work with ld.bfd and lld across a 3 GiB gap.  See that README for details.

gas accepts the MOV[NZ]-class operators on MOVZ/MOVN only, with one exception:
:gotoff_g3: is also accepted on MOVK.  GCC loads a GOT offset top chunk first
(movz :gotoff_g3:, then MOVKs down to :gotoff_g0_nc:) and never needs it, but
LLVM's textual output builds the offset low chunk first (its codegen patch
0003) and ends with
"movk xN, #:gotoff_g3:sym"; the binutils series originally rejected that, so
clang -S output could not be assembled by gas.  Linkers leave a MOVK a MOVK,
so both orders are valid.  Do not move :gotoff_g3: or other MOV[NZ] operators
onto different instructions in GCC output without checking that gas accepts
them.


Known limitations (shared with LLVM unless noted)
------------------------------------------------

- AArch64 general/local-dynamic TLS unsupported (sorry).  Note that a
  block-scope "extern __thread" redeclaration drops a tls_model attribute in
  GCC's C front end (pre-existing), which then selects GD.
- RISCV TLS keeps the GOT-relative +/-2 GiB sequences.
- Older unwinders (libgcc without patch 09) do not binary search 64-bit
  .eh_frame_hdr tables and fall back to a linear scan of .eh_frame, which is
  correct but slower.  LLVM libunwind supports them.
- PLT stubs, and lld's AArch64 PIE range-extension thunks, are ADRP-based, so
  in dynamically linked programs calls to the PLT still need the PLT within
  4 GiB of the caller; this is a linker property, not code generation.
- .eh_frame section flags: a correctly configured GCC emits .eh_frame as
  "a", PIC or not, because every EH pointer here is PC-relative, and LLVM's
  integrated assembler assembles that output (checked with llvm-mc on GCC
  16.2.0 C++ output for both targets).  An earlier version of this README
  said GCC emits "aw" under -fPIC because EH_TABLES_CAN_BE_READ_ONLY is not
  defined for these targets, and that llvm-mc then rejects the output.  That
  was an artifact of the original 16.0.1 test compilers: no target defines
  EH_TABLES_CAN_BE_READ_ONLY; its default comes from a configure-time check
  (HAVE_LD_RO_RW_SECTION_MIXING) that needs the assembler, linker and objdump,
  and only when that check fails is .eh_frame always "aw".  Seeing "aw"
  therefore means GCC was configured without working binutils.

