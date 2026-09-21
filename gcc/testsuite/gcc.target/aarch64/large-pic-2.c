/* Symbols that bind locally, labels and string literals are addressed with
   a 64-bit PC-relative displacement and do not use the GOT.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-O2 -mcmodel=large -fPIC -fno-section-anchors" } */

static int local_var;
__attribute__ ((visibility ("hidden"))) int hidden_var[16];

int *
f1 (void)
{
  return &local_var;
}

int *
f2 (void)
{
  return &hidden_var[5];
}

const char *
f3 (void)
{
  return "string";
}

static int
callee (int x)
{
  return x;
}

int (*
f4 (void)) (int)
{
  return callee;
}

/* { dg-final { scan-assembler "movz\tx\[0-9\]+, #:prel_g3:local_var\\+4\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:prel_g0_nc:local_var\\+16\n" } } */
/* { dg-final { scan-assembler "movz\tx\[0-9\]+, #:prel_g3:hidden_var\\+24\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:prel_g0_nc:hidden_var\\+36\n" } } */
/* { dg-final { scan-assembler "movz\tx\[0-9\]+, #:prel_g3:\\.LC\[0-9\]+\\+4\n" } } */
/* { dg-final { scan-assembler "movz\tx\[0-9\]+, #:prel_g3:callee\\+4\n" } } */
/* { dg-final { scan-assembler-not "gotoff" } } */
/* { dg-final { scan-assembler-not "_GLOBAL_OFFSET_TABLE_" } } */
/* { dg-final { scan-assembler-not "adrp" } } */
