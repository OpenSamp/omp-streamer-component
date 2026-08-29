# boost-cxx20 override

The vendored Boost (`lib/boost`, scipy/boost-headers-only, **1.75**) predates C++20
and does not compile on modern clang (>= 16, and hard-fails on clang 20). Rather
than fork or bump the whole Boost submodule, we shadow the few offending headers:
this directory is placed **before** `lib/boost` on the include path (see
`src/CMakeLists.txt`), so these copies win the `#include` lookup while every other
Boost header still comes from the pristine submodule.

## What is patched

`boost/numeric/conversion/{int_float_mixture,sign_mixture,udt_builtin_mixture}_enum.hpp`

Each enum gained a **fixed underlying type** (`: int`). Boost's `mpl::integral_c`
machinery does `static_cast<the_enum>(value - 1)`, which yields `-1` — outside the
enum's value range. For an unscoped enum **without** a fixed underlying type that is
a non-constant expression in C++20, so clang rejects it as a non-type template
argument (`error: non-type template argument is not a constant expression`). With a
fixed underlying type the out-of-range cast is well-defined. This matches what
upstream Boost did in later releases. Behaviour is otherwise identical (values 0..3).

## When this can go away

Delete this directory and its `target_include_directories(... BEFORE ...)` line once
`lib/boost` is bumped to a release that already carries the fix (Boost >= ~1.80).
