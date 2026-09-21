/* General-dynamic TLS has no large-model access sequence.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-require-effective-target tls_native } */
/* { dg-options "-O2 -mcmodel=large -fPIC" } */

extern __thread int gd_var; /* { dg-message "sorry, unimplemented: general-dynamic and local-dynamic TLS models are not supported" } */
/* { dg-message "use '-ftls-model=initial-exec'" "" { target *-*-* } .-1 } */

int
f (void)
{
  return gd_var;
}
