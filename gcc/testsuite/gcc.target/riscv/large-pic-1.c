/* Preemptible symbols are reached through a slot in .data.rel.ro, whose
   displacement from the literal pool entry is a link-time constant.  */
/* { dg-do compile } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-march=rv64gc -mabi=lp64d -mcmodel=large -fPIC -fno-section-anchors" } */
/* { dg-skip-if "" { *-*-* } { "-O0" "-flto" } } */

extern int ext_var;

int
f (void)
{
  return ext_var;
}

/* { dg-final { scan-assembler "\\.section\t\\.data\\.rel\\.ro" } } */
/* { dg-final { scan-assembler "\\.dword\text_var\n" } } */
/* { dg-final { scan-assembler "\\.dword\t\\.LC\[0-9\]+-\\.\n" } } */
/* { dg-final { scan-assembler "lla\t\[a-z0-9\]+,\\.LC\[0-9\]+\n" } } */
/* { dg-final { scan-assembler-not "\tla\t" } } */
