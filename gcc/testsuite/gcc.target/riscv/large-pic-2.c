/* Symbols that bind locally are reached with a displacement held in the
   literal pool, with no slot and no dynamic relocation.  */
/* { dg-do compile } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-march=rv64gc -mabi=lp64d -mcmodel=large -fPIC -fno-section-anchors" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } } */

static int local_var;
__attribute__ ((visibility ("hidden"))) int hidden_var;

int *
f1 (void)
{
  return &local_var;
}

int *
f2 (void)
{
  return &hidden_var;
}

const char *
f3 (void)
{
  return "string";
}

/* { dg-final { scan-assembler "\\.dword\tlocal_var-\\.\n" } } */
/* { dg-final { scan-assembler "\\.dword\thidden_var-\\.\n" } } */
/* { dg-final { scan-assembler "\\.dword\t\\.LC\[0-9\]+-\\.\n" } } */
/* { dg-final { scan-assembler-not "\\.data\\.rel\\.ro" } } */
/* { dg-final { scan-assembler-not "\tla\t" } } */
