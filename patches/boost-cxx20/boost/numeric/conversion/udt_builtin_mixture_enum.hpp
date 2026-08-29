//  (c) Copyright Fernando Luis Cacciola Carballal 2000-2004
//  Use, modification, and distribution is subject to the Boost Software
//  License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

//  See library home page at http://www.boost.org/libs/numeric/conversion
//
// Contact the author at: fernando_cacciola@hotmail.com
//
// VS:RP override for C++20 / clang >= 16 — fixed underlying type so boost::mpl's
// static_cast<udt_builtin_mixture_enum>(-1) stays a valid constant expression.
// See the sibling int_float_mixture_enum.hpp for the full rationale.
//
#ifndef BOOST_NUMERIC_CONVERSION_UDT_BUILTIN_MIXTURE_ENUM_FLC_12NOV2002_HPP
#define BOOST_NUMERIC_CONVERSION_UDT_BUILTIN_MIXTURE_ENUM_FLC_12NOV2002_HPP

namespace boost { namespace numeric
{
  enum udt_builtin_mixture_enum : int
  {
     builtin_to_builtin
    ,builtin_to_udt
    ,udt_to_builtin
    ,udt_to_udt
  } ;

} } // namespace boost::numeric

#endif
//
///////////////////////////////////////////////////////////////////////////////////////////////
