// PR ipa/119006
// { dg-do run { target c++11 } }
// { dg-options "-O2 -fwhole-program" }

struct A {
  bool operator== (const char *x) const { return x && !__builtin_strcmp (a, x); }
  char a[11];
};

struct B {
  bool operator== (const char *x) const { return x && !__builtin_strcmp (a, x); }
  bool operator!= (const char *x) const { return !(*this == x); }
  char a[128];
};

[[gnu::noinline,gnu::used]] int
foo (const A& lhs, const char* rhs)
{
  return lhs == rhs;
}

constexpr const char *t = "abcdefghijklmno";

[[gnu::noinline,gnu::used]] void
bar (B x)
{
  if (x != t) __builtin_abort ();
}

int
main ()
{
  B b;
  __builtin_strcpy (b.a, t);
  bar (b);
}
