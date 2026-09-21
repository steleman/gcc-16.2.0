/* Calls load their target through the literal pool rather than using a
   +/-2GiB PC-relative call.  */
/* { dg-do compile } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-march=rv64gc -mabi=lp64d -mcmodel=large -fPIC" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } } */

extern void ext_func (void);
static void local_func (void) { }

void
f (void)
{
  ext_func ();
  local_func ();
}

/* { dg-final { scan-assembler "\\.dword\text_func\n" } } */
/* { dg-final { scan-assembler "jalr" } } */
/* { dg-final { scan-assembler-not "call\t" } } */
/* { dg-final { scan-assembler-not "@plt" } } */
