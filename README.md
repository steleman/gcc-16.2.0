# `-mcmodel=large` with `-fpic`/`-fPIC` for AArch64 and RISCV64 -- GCC 16.2.0

This series adds a position-independent large code model to GCC for AArch64
and RV64, so shared libraries and position-independent executables can place
code, data and the GOT arbitrarily far apart. Upstream GCC rejects
`-mcmodel=large` together with `-fpic`, `-fPIC`, `-fpie` or `-fPIE` on both
architectures.

The code sequences and relocations are the ones LLVM 23.1.1 uses for the same
model (the companion LLVM series), so objects from GCC and Clang can be mixed
in one link, and the result links with ld.bfd, ld.gold and lld.

Neither model is part of a published ABI. AAELF64 defines no large PIC code
model, and the RISCV psABI currently forbids the large code model with PIC.
Both are extensions that GCC, LLVM and binutils implement the same way.

## Contents

| Patch | Area | Summary |
|---|---|---|
| 0001 | AArch64 | Large PIC code model: PC-relative (`MOVW_PREL_G*`) and GOT (`MOVW_GOTOFF_G*`) addressing, GOT base computed once per function |
| 0002 | AArch64 | Initial-exec TLS through `TLSIE_MOVW_GOTTPREL_G1/_G0_NC`; general-/local-dynamic TLS rejected with `sorry` |
| 0003 | AArch64 | Accept `-mcmodel=large` with PIC on ELF; `invoke.texi` |
| 0004 | AArch64 | Tests `gcc.target/aarch64/large-pic-{1..8}.c` |
| 0005 | RISCV | Large PIC model: self-relative literal-pool entries plus `.data.rel.ro` slots |
| 0006 | RISCV | Accept `-mcmodel=large` with PIC on RV64; `invoke.texi` |
| 0007 | RISCV | Tests `gcc.target/riscv/large-pic-{1..4}.c` |
| 0008 | RISCV | 8-byte PC-relative EH pointers (FDE, personality, LSDA) in the large model, PIC and non-PIC |
| 0009 | libgcc | Binary search of `DW_EH_PE_datarel\|sdata8` `.eh_frame_hdr` tables |

Apply in order from the top of a GCC 16.2.0 tree:

```sh
for p in 00*.patch; do patch -p1 < $p; done
```

The patches apply to the pristine `gcc-16.2.0.tar.xz` with no offsets or fuzz.
The Fedora 44 patch files touch none of the same files.

### Toolchain requirements

| Component | AArch64 | RISCV64 |
|---|---|---|
| Assembler | GNU as with the binutils series (`:gotoff_gN:` operators), or LLVM's integrated assembler | Any; no new relocations |
| ld.bfd | Binutils series (`MOVW_GOTOFF_G*`, MOV[NZ]/MOVK handling) | Binutils series patch 0005 for 8-byte `.eh_frame` fields; patch 0004 for code or `.eh_frame` more than 2 GiB from `.eh_frame_hdr` |
| ld.gold | Binutils series patch 0003 | No RISCV port |
| lld | LLVM series patch 0002 (`MOVW_GOTOFF_G*`, `MOVW_GOTTPREL`) | Stock |
| Unwinder | Stock | libgcc with patch 0009 (or LLVM libunwind) to binary search 64-bit `.eh_frame_hdr` tables; older libgcc falls back to a linear scan |

---

## AArch64 (patches 0001–0004)

### Code model selection

Patch 03 changes `initialize_aarch64_code_model`. When `-mcmodel=large` is
combined with any PIC or PIE flag on ELF, it selects the new
`AARCH64_CMODEL_LARGE_PIC` (`aarch64-opts.h`) instead of reporting `sorry`.
PE-COFF keeps the `sorry`, and ILP32 is still rejected. The preprocessor
defines `__AARCH64_CMODEL_LARGE__`, as for the non-PIC large model.

### GOT base

AAELF64 has no relocation that reaches the GOT with more than ±4 GiB of range.
The GOT base is therefore computed PC-relatively, from an `ADR` plus a 64-bit
displacement built in four `R_AARCH64_MOVW_PREL_G*` chunks. This mirrors x86-64
large PIC, which uses `leaq .L$pb(%rip)` plus `movabsq`:

```asm
.Lpc:
  adr  xD, .Lpc
  movz tmp, #:prel_g3:_GLOBAL_OFFSET_TABLE_+4
  movk tmp, #:prel_g2_nc:_GLOBAL_OFFSET_TABLE_+8
  movk tmp, #:prel_g1_nc:_GLOBAL_OFFSET_TABLE_+12
  movk tmp, #:prel_g0_nc:_GLOBAL_OFFSET_TABLE_+16
  add  xD, xD, tmp
```

`MOVW_PREL_G*` resolves as `S + A - P`, where `P` is the address of the
instruction being relocated. Each chunk therefore carries an addend equal to
its own distance from the `ADR` (+4, +8, +12, +16), so every chunk is a slice
of the same value, `_GLOBAL_OFFSET_TABLE_ - .Lpc`.

Implementation:

- The six instructions are a single insn, `aarch64_load_large_pic_prel`
  (`aarch64.md`), with a `match_scratch` for `tmp`. They cannot be split,
  because the addends depend on their relative positions.
  `aarch64_output_large_pic_prel` prints the sequence.
- The GOT base lives in the pseudo PIC register (`TARGET_USE_PSEUDO_PIC_REG`
  now returns true for this model). `aarch64_init_pic_reg`
  (`TARGET_INIT_PIC_REG`) computes it once per function on the entry edge, and
  only if the function uses it, as i386 does for its large PIC model.
- The pseudo register exists only before reload, so every large-PIC expansion
  path asserts `can_create_pseudo_p ()`.
- `aarch64_large_pic_got_base` returns a throwaway register when IVOPTs
  expands addresses for costing before RTL expansion (`pic_offset_table_rtx`
  is still `NULL`).

### Symbol access

`aarch64_classify_symbol` classifies every symbol in this model, and
`aarch64_load_symref_appropriately` expands each class:

| Symbol | Class | Sequence |
|---|---|---|
| Preemptible (does not bind locally) | `SYMBOL_LARGE_PIC_GOT` | `movz tmp, #:gotoff_g3:sym` / `movk #:gotoff_g2_nc:` / `movk #:gotoff_g1_nc:` / `movk #:gotoff_g0_nc:` / `ldr xD, [gp, tmp]` |
| Binds locally, labels, jump tables, string constants | `SYMBOL_LARGE_PIC_PREL` | The six-instruction `ADR` + `MOVW_PREL` sequence against the symbol; its displacement also absorbs any constant offset |
| Constant-pool entries | `SYMBOL_SMALL_ABSOLUTE` (or `TINY` with PC-relative literal loads) | Per-function literal pool in the function's own section, as in the non-PIC large model |

- The GOT offset pattern is `aarch64_large_pic_movw_gotoff`. The offset is
  built top chunk first, with `MOVZ` carrying `:gotoff_g3:`.
- `aarch64_can_use_per_function_literal_pools_p` returns true for the model.
  Symbolic constants are never forced into a pool, so pools carry no
  relocations.
- Calls are unchanged: `BL`/`B` with `R_AARCH64_CALL26`, which the linker
  extends with veneers or the PLT.
- `aarch64_rtx_costs` charges four extra instructions for a symbol reference,
  so the optimizers see the real cost.
- Exception tables already use 8-byte PC-relative encodings in the AArch64
  large model (`R_AARCH64_PREL64` for personality and LSDA).

### Thread-local storage (patch 0002)

| Model | Handling |
|---|---|
| Local-exec | Unchanged. The TPREL sequence is relative to the thread pointer and never uses ADRP. |
| Initial-exec | `SYMBOL_LARGE_PIC_TLSIE`: `movz tmp, #:gottprel_g1:sym` / `movk #:gottprel_g0_nc:sym` / `ldr tmp, [gp, tmp]` / `mrs tp, tpidr_el0` / `add xD, tp, tmp` (pattern `aarch64_large_pic_movw_gottprel`) |
| General-/local-dynamic | `sorry` from `aarch64_check_large_pic_tls`, with a note suggesting `-ftls-model=initial-exec` or `local-exec`. TLSDESC and `__tls_get_addr` have only ADRP-based sequences, which would silently bring back the ±4 GiB limit. |

AAELF64 defines only two `GOTTPREL` MOVW chunks, so the GOT itself is limited
to 4 GiB, but the image is not. Linkers must not relax this pair from IE to
LE: the `LDR` that consumes the offset has no relocation and cannot be
rewritten.

### Relocations in AArch64 objects

`R_AARCH64_MOVW_PREL_G0_NC`, `_G1_NC`, `_G2_NC`, `_G3`;
`R_AARCH64_MOVW_GOTOFF_G0_NC`, `_G1_NC`, `_G2_NC`, `_G3`;
`R_AARCH64_TLSIE_MOVW_GOTTPREL_G1`, `_G0_NC`; `R_AARCH64_PREL64` (EH data);
`R_AARCH64_CALL26`. `_GLOBAL_OFFSET_TABLE_` must be the start of `.got`, which
holds for ld.bfd and lld. gold biases the symbol by 0x8000 for large GOTs, but
computes GOT offsets from the same biased symbol, so the sequence still works.

### Tests (patch 0004)

`gcc.target/aarch64/large-pic-1.c` … `-8.c` check the GOT base sequence and
its addends, local/hidden/string/call-target PC-relative access, a single GOT
base per function, GOT access for preemptible symbols, IE and LE TLS, the
GD/LD `sorry`, the predefined macro, jump tables without ADRP, and the GOT
and PC-relative forms again at `-O0`.

---

## RISCV64 (patches 0005 - 0008)

### Why the non-PIC large model cannot simply be reused

The RISCV large model loads addresses from a per-function literal pool in the
function's section, within `auipc` range of the code. Under PIC, those
absolute addresses would need dynamic relocations, which cannot live in
`.text`. Moving them to a writable section breaks the range guarantee.

### Sequences (patch 0005)

The pool is split in two: the part that needs a dynamic relocation, and the
part that must be near the code. A link-time constant connects them, and no
new relocation types are needed.

Symbol that binds locally: the pool entry holds the displacement from the
entry itself to the symbol, an `R_RISCV_ADD64`/`R_RISCV_SUB64` pair that the
static linker resolves.

```asm
  lla  a0, .LC0            # a0 = &entry
  ld   t0, 0(a0)           # t0 = sym - &entry
  add  a0, a0, t0          # a0 = sym
.LC0:
  .dword sym-.
```

Preemptible symbol: its address lives in a slot in `.data.rel.ro`, which
takes the dynamic relocation (`R_RISCV_64`, or `R_RISCV_RELATIVE` for hidden
symbols). The pool entry holds the displacement to the slot.

```asm
  lla  a0, .LC1
  ld   t0, 0(a0)
  add  a0, a0, t0          # a0 = &slot
  ld   a0, 0(a0)           # a0 = sym
.LC1:
  .dword .LC2-.
  .section .data.rel.ro
.LC2:
  .dword sym
```

Implementation in `gcc/config/riscv/`:

- `riscv_large_pic_p ()` is true for `CM_LARGE` with `flag_pic` and gates
  everything else. Under PIC, `riscv_cmodel` stays `CM_LARGE` instead of
  becoming `CM_PIC` (patch 06).
- `riscv_classify_symbol` returns `SYMBOL_FORCE_TO_MEM` for symbols and
  `SYMBOL_PCREL` for labels and constant-pool entries.
- `riscv_legitimize_large_pic_move` builds the pool constants. Each is a
  `(const (unspec [...] UNSPEC_LARGE_PIC_DISP))`, wrapping a
  `(const (unspec [...] UNSPEC_LARGE_PIC_SLOT))` for preemptible symbols. It
  emits `large_pic_load_address` or `large_pic_load_got` (`riscv.md`) with a
  clobbered scratch register.
- `riscv_output_addr_const_extra` (`TARGET_ASM_OUTPUT_ADDR_CONST_EXTRA`)
  prints the displacement as `sym-.` and the slot as `sym`.
- GCC's constant pool picks a section per entry.
  `riscv_elf_select_rtx_section` sends slot entries to the default relocatable
  read-only section (`.data.rel.ro`) and keeps displacement entries in the
  function's section.
- `riscv_cannot_force_const_mem` rejects the raw symbolic constants, so an
  absolute address never reaches the per-function pool.
  `riscv_const_insns` returns 0 for them, so they are never rematerialized
  after reload.
- Thunks are the only code that materializes addresses after reload. They use
  the prologue temporaries (t0, or t2 when the destination is t0).
- Call targets use the same sequences, since the large model already calls
  through a register. Calls to preemptible functions go through the
  `.data.rel.ro` slot, so interposition works without a PLT.
- TLS keeps the usual GOT-relative PC-relative sequences, which limit the
  distance between code and GOT to ±2 GiB, as in LLVM.

### Option acceptance (patch 0006)

`riscv_option_override` no longer reports `sorry` for `-mcmodel=large` with
PIC. `invoke.texi` documents the model and notes that it is outside the psABI.

### Tests (patch 0007)

`gcc.target/riscv/large-pic-1.c` … `-4.c` check the local displacement
entry, the `.data.rel.ro` slot for preemptible data and functions, the
absence of `la`, GOT and PLT references, and the `__riscv_cmodel_large`
predefine.

### Exception handling (patch 0008)

The large model makes no assumption about the distance between code and
`.eh_frame`, but RISCV always used 4-byte PC-relative EH pointers. This was
worse than a range limit: with code more than 2 GiB away, lld rejects
`R_RISCV_32_PCREL`, while ld.bfd links without a diagnostic and produces FDEs
that decode to the wrong address, so unwinding fails at run time.

- `ASM_PREFERRED_EH_DATA_FORMAT` (`riscv.h`) selects `DW_EH_PE_sdata8` for
  `CM_LARGE`, PIC and non-PIC. There is no 64-bit PC-relative data
  relocation, so the pointers are emitted as `ADD64`/`SUB64` pairs, which
  survive linker relaxation. medlow and medany are unchanged.
- `riscv_option_override` defaults `flag_dwarf2_cfi_asm` to 0 for `CM_LARGE`,
  so dwarf2out writes `.eh_frame` itself: CIE augmentation `zR`/`zPLR` with
  FDE encoding 0x1c, personality 0x9c and LSDA 0x1c. When gas builds
  `.eh_frame` from `.cfi_*` directives, it always encodes an FDE's initial
  location as pcrel|sdata4, whatever the compiler prefers. An explicit
  `-fdwarf2-cfi-asm` is honoured, and gives 4-byte FDE pointers.
- Tests: `gcc.target/riscv/large-model-eh-1.c` (non-PIC) and `-2.c` (PIC).

The directive default was added while porting to 16.2.0. The 16.0.1 series
originally had only the encoding change, which left FDE pointers 4-byte
whenever GCC used CFI directives, as it does with any assembler that supports
them. That version failed its own tests and failed to unwind with code 3 GiB
from `.eh_frame`.

### Relocations in RISCV objects

`.text`: `R_RISCV_PCREL_HI20`/`_LO12_I` (`lla`), `R_RISCV_ADD64`/`SUB64`
(pool entries). `.data.rel.ro`: `R_RISCV_64`. `.eh_frame`:
`R_RISCV_ADD64`/`SUB64`. The code section needs no dynamic relocations.

---

## libgcc (patch 0009)

When code or `.eh_frame` is more than 2 GiB from `.eh_frame_hdr`, linkers
write the binary search table with `DW_EH_PE_datarel|DW_EH_PE_sdata8` entries
(encoding 0x3c) and `eh_frame_ptr` as 8 bytes (0x1c). lld does this, and so
does ld.bfd with binutils patch 04. The first version of that patch widened
only the table and silently truncated `eh_frame_ptr` when `.eh_frame` itself
was far away, which made libgcc without this patch segfault in its linear
scan; it was fixed on 2026-09-17. `_Unwind_Find_FDE`
in `unwind-dw2-fde-dip.c` only binary searched `sdata4` tables, and fell back
to a linear scan of `.eh_frame` on every lookup for anything else.

`find_fde_tail` now dispatches 0x3c tables on 64-bit targets to
`find_fde_sdata8_table`. That function binary searches the 16-byte entries,
reading them with `memcpy` because the table follows a 4-byte count and is
not necessarily 8-byte aligned. It then checks the PC against the FDE's range
(`fde_range`). Tables with 32-bit entries take the existing path. The change
is not specific to the large model: it helps any binary whose linker emits a
64-bit table.

---

## Compatibility with LLVM and lld

### AArch64

The companion LLVM (`llvm-project-mcmodel-large`, patches 0001 - 0004 on
[LLVM 23.1.1](https://github.com/steleman/llvm-23.1.1)) implements the same model:

| Aspect | GCC | Clang/LLVM | Compatible |
|---|---|---|---|
| GOT base | `ADR` + `MOVW_PREL_G3..G0_NC` with addends +4…+16 | Same, scratch `x17` | Yes, identical relocations |
| GOT offset | Top chunk first: `movz #:gotoff_g3:`, then `movk` down to `:gotoff_g0_nc:` | Low chunk first: `movz #:gotoff_g0_nc:` … `movk #:gotoff_g3:` | Yes, with conditions (see below) |
| Local symbols | PC-relative sequence against the symbol | Same | Yes |
| IE TLS | `MOVW_GOTTPREL_G1/_G0_NC` indexing the GOT base | Same (MOVN placeholder under `:gottprel_g1:`) | Yes, linker picks MOVZ/MOVN by sign |
| GD/LD TLS | `sorry` | Error | Neither supports it |
| Pointer authentication (signed GOT) | Not supported | Diagnosed | Neither supports it |
| Assembler operators | `:gotoff_g0:` … `:gotoff_g3:` (binutils series) | Same spellings (LLVM 0001) | GNU as and llvm-mc emit identical relocations for the same source |

Two details make mixed objects work, and any future specification needs to pin
them down:

1. **A MOVK stays a MOVK.** `MOVW_GOTOFF_G0..G3` and
   `TLSIE_MOVW_GOTTPREL_G1` are MOV[NZ]-class relocations: the linker chooses
   MOVZ or MOVN from the sign of the value. Clang puts `GOTOFF_G3` on a MOVK.
   lld, ld.bfd (binutils 01) and ld.gold (binutils 03) all choose by sign
   unless the instruction is already a MOVK, in which case they patch only the
   immediate. GCC's top-chunk-first order works under either rule.
2. **`:gotoff_g3:` is accepted on MOVK by GNU as** (binutils 02), so textual
   `clang -S`/`llc` output assembles with gas. Other MOV[NZ]-class operators
   stay MOVZ/MOVN-only. Don't move GCC's `:gotoff_g3:` or other MOV[NZ]
   operators onto different instructions without checking that gas accepts
   them.

All three linkers leave the MOVW GOTTPREL pair unrelaxed.

### RISCV64

LLVM's RISCV large PIC model is a prototype behind the hidden option
`-riscv-large-pic` (LLVM series 0002 - 0004). By default the driver rejects
`-mcmodel=large` with PIC (LLVM 0001), so Clang objects are built with
`clang -emit-llvm` and then
`llc -code-model=large -relocation-model=pic -riscv-large-pic`.

| Aspect | GCC | LLVM prototype | Compatible |
|---|---|---|---|
| Local symbols | Per-use pool entry `.dword sym-.` | Per-function anchor in `.text` to a table | Link-level: both use only `ADD64`/`SUB64` in `.text` |
| Preemptible symbols | Per-symbol slot in `.data.rel.ro`, reached through a pool displacement | Per-function table `.Lrvlp_tbl.<fn>` in `.data.rel.ro`, reached through one anchor (`auipc`/`addi`, `ld`, `add`, then `ld` per symbol) | Link-level: both use `R_RISCV_64` in `.data.rel.ro` |
| New relocations | None | None | Yes |
| TLS | GOT-relative, ±2 GiB | Same | Yes |
| EH pointers | pcrel\|sdata8 (0x1c/0x9c) as `ADD64`/`SUB64` | Same (LLVM `mcmodel-large-eh-riscv` patch; `llvm-mc` uses sdata8 FDEs only with `-large-code-model`) | Yes |
| Jump tables | In the function's section, reached with `lla` (upstream: `JUMP_TABLES_IN_TEXT_SECTION` for `CM_LARGE`) | Same since LLVM `98677af3cb50` (`mcmodel-large-jt-riscv`); before it, in `.rodata` behind a ±2 GiB `auipc`, which fails to link with data more than 2 GiB away | Yes, with that commit |

The code sequences differ, but compatibility is a link-level property: no
relocations or conventions cross function boundaries beyond the standard
calling convention, and interposition through `.data.rel.ro` works with
objects from either compiler.

### Linkers

| Linker | AArch64 | RISCV64 |
|---|---|---|
| lld (LLVM) | `MOVW_GOTOFF_G*`, `MOVW_GOTTPREL` pair unrelaxed; writes 64-bit `.eh_frame_hdr` tables | Stock lld; writes 64-bit `.eh_frame_hdr` tables |
| ld.bfd (Binutils) | 01: `MOVW_GOTOFF_G*`, MOV[NZ]/MOVK rule; 04: 64-bit `.eh_frame_hdr` (8-byte `eh_frame_ptr` and table) | 04: 64-bit `.eh_frame_hdr` (8-byte `eh_frame_ptr` and table); 05: fixes 8-byte `.eh_frame` fields after CIE merging (before it, exceptions silently called `std::terminate`) |
| ld.gold (Binutils) | 03: `MOVW_PREL_G*`, `MOVW_GOTOFF_G*`, `TLSIE_MOVW_GOTTPREL_*` | No RISCV port |

### Limits shared by all toolchains

- AArch64: no general-/local-dynamic TLS, no signed GOT; the GOT is limited
  to 4 GiB for initial-exec TLS.
- PLT stubs, and lld's AArch64 PIE range-extension thunks, are ADRP-based,
  so in dynamically linked programs, calls through the PLT still need the PLT
  within 4 GiB of the caller. This is a linker property, not code generation.
- RISCV TLS is limited to ±2 GiB between code and GOT.
- AArch64 and x86-64 large models emit `.cfi_*` directives, so their FDE
  initial locations stay 4-byte. This is pre-existing upstream behavior, not
  changed here. LLVM's integrated assembler uses 8-byte ones for its large
  model, so on AArch64 only Clang objects built by it can place code more than
  2 GiB from `.eh_frame`; mixed objects otherwise link and unwind normally.
- `.eh_frame` section flags are not a limit. A correctly configured GCC emits
  `.eh_frame` as `"a"`, PIC or not, because every EH pointer here is
  PC-relative, and `llvm-mc` assembles the output (checked on GCC 16.2.0 C++
  output for both targets). An earlier version of this README said GCC emits
  `"aw"` under `-fPIC` and that LLVM's assembler then rejects the output; that
  came from the original 16.0.1 test compilers. `EH_TABLES_CAN_BE_READ_ONLY`
  defaults to the result of a configure-time linker check
  (`HAVE_LD_RO_RW_SECTION_MIXING`, which needs the assembler, linker and
  objdump), and only when that check fails is `.eh_frame` always `"aw"`.
  Seeing `"aw"` means GCC was configured without working binutils.

---

## Verification on GCC 16.2.0

Cross compilers for `aarch64-linux-gnu` and `riscv64-linux-gnu` were built
from the pristine tarball (baseline) and from the patched tree. They were
tested with the binutils series, the patched clang/lld 23.1.1, glibc sysroots
and qemu-user. All four builds' configure found working binutils:
`gcc_cv_as_cfi_directive=yes` and `gcc_cv_ld_ro_rw_mix=read-write` in
`gcc/config.log`. Check those before trusting results from any GCC build: one
whose probes failed emits `.eh_frame` as `"aw"` and probably no `.cfi_*`
directives, which hides bugs such as the original patch 08's.

| Check | Result |
|---|---|
| New tests, DejaGnu directives emulated (RISCV counted per torture option) | AArch64 8/8, RISCV 29/29; all fail with the baseline |
| Existing `large-model.c`, `jump-table-large-code-model.c` | Pass |
| Differential compile, baseline vs patched, same flags | 4140 `gcc.target/aarch64`, 6539 `gcc.target/riscv`, about 1615 `gcc.c-torture/compile` per architecture: assembly identical apart from debug-info paths and LTO bytecode, except the three RISCV `-mcmodel=large` tests, which change as intended by patch 08 |
| Stress: `gcc.dg` + `gcc.c-torture/{compile,execute}` with `-mcmodel=large -fPIC`, `-O2`/`-O0` | 22,240 compilations per architecture, no ICEs. New failures: only the intended GD/LD `sorry` (AArch64) and `gcc.dg/pr119493-2.c` (RISCV musttail), which fails identically in the baseline non-PIC large model |
| Runtime ABI matrix: `-fPIC` DSO + `-fPIE` executable, {GCC, Clang} × {GCC, Clang}, `-O2`/`-O0`; interposition, weak undefined, jump tables, computed goto, FP constants, large offsets, IE/LE TLS | AArch64 24/24 (ld.bfd, ld.gold, lld); RISCV 16/16 (ld.bfd, lld) |
| C++ exceptions through destructors and callbacks across DSO and PIE | AArch64 6/6, RISCV 4/4 |
| RISCV: library `.text` 3 GiB above `.eh_frame` | Exceptions caught with lld and ld.bfd; with `-fdwarf2-cfi-asm`, lld rejects the link and ld.bfd output calls `std::terminate` |
| AArch64 far data layout: freestanding static programs with all data and the GOT 5 GiB above `.text`, same checks as the RISCV row below | Large PIC links and runs at `-O2` and `-O0` with ld.bfd, ld.gold and lld for every {GCC, Clang} × {GCC, Clang} object combination (24/24); the small model fails to link with all three. `.text` relocations are only `MOVW_PREL_G*`, `MOVW_GOTOFF_G*` and `CALL26` |
| AArch64, Clang textual output through gas: Clang objects built with `clang -fno-addrsig -S` (output contains `movk xN, #:gotoff_g3:sym`) and assembled by gas | Runtime ABI matrix 24/24 and 5 GiB far layout 24/24, both with ld.bfd, ld.gold and lld. Compared with integrated-assembler objects, the code differs only in the MOVZ/MOVN placeholder under `:gottprel_g1:`, which linkers rewrite by sign. gas gives FDE initial locations `PREL32` (from `.cfi_*`), where the integrated assembler uses `PREL64` |
| libgcc 64-bit `.eh_frame_hdr` search (patch 09): a large-PIC DSO with 200 functions 3 GiB above `.eh_frame_hdr` (RISCV: code 3 GiB above `.eh_frame` too; AArch64: `.eh_frame` kept with the code, since its gas-built FDEs are 4-byte), each looked up with `_Unwind_Find_FDE` | Linkers write a 0x3c table. Intact table: 200/200 correct, distinct FDEs with patched and baseline libgcc_s. Every table entry's FDE redirected to the first: patched libgcc_s fails 199/200 (it uses the binary search), baseline still succeeds by linear scan. RISCV and AArch64, ld.bfd and lld. (AArch64 with ld.bfd first showed baseline libgcc_s segfaulting, because binutils patch 04 then wrote a truncated 4-byte `eh_frame_ptr` with `.eh_frame` 3 GiB from the header; patch 04 now writes 8 bytes, as lld does) |
| RISCV far data layout: freestanding static programs with all data, `.data.rel.ro` slots and constants 3 GiB above `.text`; preemptible and hidden globals, statics, a large array, FP constants, strings, local and extern function pointers, jump table, computed goto, writes | Large PIC links and runs with ld.bfd and lld at `-O2` and `-O0` whenever the objects are GCC-only, or GCC mixed with Clang `-riscv-large-pic` objects (10/10; the 2 remaining mixed links used a Clang `-O0` object whose jump table was out of range, a gap in LLVM's RISCV large model since fixed by LLVM commit `98677af3cb50`, after which all {GCC, Clang} × {GCC, Clang} links pass, 16/16); medany fails to link with both linkers. GCC `.text` relocations are only `PCREL_HI20/LO12` to its own literal pool and `ADD64`/`SUB64` displacements |
| The same tests at `-O1` and `-O3` | Stress (22,240 compilations per architecture): no ICEs; new failures only the GD/LD `sorry` (AArch64, 34 files) and `pr119493-2.c` (RISCV musttail, identical in the baseline non-PIC large model). Runtime ABI matrix AArch64 24/24 (also 24/24 with Clang textual output through gas), RISCV 16/16; C++ exceptions (GCC objects only) AArch64 6/6, RISCV 4/4; the RISCV far EH layout and the exception tests with Clang objects (from the original 16.0.1 testing) were not repeated. Far data: AArch64 5 GiB 24/24 (integrated assembler and gas), RISCV 3 GiB 16/16; jump-table switch GCC 8/8 and Clang 8/8 with LLVM `98677af3cb50` (0/8 without); controls fail to link. `.eh_frame_hdr` search: unchanged, patched libgcc uses the 64-bit table on all four target/linker combinations. New tests: RISCV torture runs at `-O1`/`-O3 -g` 12/12; AArch64 with the level appended 7/8, the exception being `large-pic-6.c`, the deliberate `-O0` variant, whose never-written `static int` folds away (no ADRP in the remaining code) |
| Build | No new warnings in the patched files |

Upstream changes between 16.0.1 and 16.2.0 that could have interacted with the
series were reviewed and needed no change:
- RISCV t1 was removed from `SIBCALL_REGS`; the thunk path uses t0/t2.
- The RISCV stack-protector patterns now use `riscv_output_move`.
- AArch64 has a new `narrow_gp_writes` pass, which cannot narrow the large-PIC
  UNSPEC/MEM loads.

See the [README.MCMODEL-LARGE](README.MCMODEL-LARGE.txt) for more details.

## Related work

| Series | Location |
|---|---|
| Binutils 2.46.1 | [GNU Binutils 2.46.1](https://github.com/steleman/binutils-2.46.1): (0001 bfd/ld GOTOFF relocations, 0002 gas operators, 0003 gold, 0004 64-bit `.eh_frame_hdr` with 8-byte `eh_frame_ptr`, 0005 RISCV `.eh_frame` fix.) |
| LLVM AArch64 | [LLVM 23.1.1](https://github.com/steleman/llvm-23.1.1): (MC operators, lld relocations, codegen, driver.) |
| LLVM RISCV | [LLVM 23.1.1](https://github.com/steleman/llvm-23.1.1): (prototype behind `-riscv-large-pic`; `DESIGN.md`), `mcmodel-large-eh-riscv` (8-byte EH encodings) and `mcmodel-large-jt-riscv` (jump tables in the function's section under the large model) |
| GCC 16.2.0 | [GCC 16.2.0](https://github.com/steleman/gcc-16.2.0) contains the patch 0008 fix. |

## Testing

- Tested on RISCV64 with Fedora 42 RISCV64 Server running in an `x86_64` Fedora 41 KVM with the system-provided `qemu`.
- Tested on AArch64 with Fedora 44 AArch64 Server running in an `x86_64`  Fedora 41 KVM with the system-provided `qemu`.

Needless to mention, testing this way is extremely slow. I do not have access to real hardware for these two ISA's of interest, so my testing capabilities are limited.

## Why am I doing this

For fun, research, learning, and in the hope that someone might find it useful or interesting.

