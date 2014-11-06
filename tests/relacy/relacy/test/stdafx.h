#ifndef STDAFX_H
#define STDAFX_H
#ifdef _MSC_VER
#   pragma once
#endif

#ifdef _MSC_VER
#    pragma warning (disable: 4127)
#endif

#if defined(_MSC_VER) && (_MSC_VER <= 1310)
//#    pragma warning (disable: 4511)
//#    pragma warning (disable: 4512)
#endif


#ifdef NDEBUG
#   define _SECURE_SCL 0
#endif

#include "../relacy/pch.hpp"


#endif

