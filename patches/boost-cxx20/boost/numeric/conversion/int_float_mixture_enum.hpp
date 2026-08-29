//  (c) Copyright Fernando Luis Cacciola Carballal 2000-2004
//  Use, modification, and distribution is subject to the Boost Software
//  License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

//  See library home page at http://www.boost.org/libs/numeric/conversion
//
// Contact the author at: fernando_cacciola@hotmail.com
//
// ---------------------------------------------------------------------------
// VS:RP override for C++20 / clang >= 16. The vendored boost 1.75 declares this
// enum without a fixed underlying type; boost::mpl::next/prior then does
// static_cast<int_float_mixture_enum>(-1), which is a non-constant expression
// under C++20 and a hard error on modern clang. Giving it a fixed underlying
// type makes the out-of-range cast well-defined. Shadowed ahead of lib/boost via
// an include-dir override (see src/CMakeLists.txt) so the submodule stays clean.
// ---------------------------------------------------------------------------
#ifndef BOOST_NUMERIC_CONVERSION_INT_FLOAT_MIXTURE_ENUM_FLC_12NOV2002_HPP
#define BOOST_NUMERIC_CONVERSION_INT_FLOAT_MIXTURE_ENUM_FLC_12NOV2002_HPP

namespace boost { namespace numeric
{
  enum int_float_mixture_enum : int
  {
     integral_to_integral
    ,integral_to_float
    ,float_to_integral
    ,float_to_float
  } ;

} } // namespace boost::numeric

#endif
//
///////////////////////////////////////////////////////////////////////////////////////////////
