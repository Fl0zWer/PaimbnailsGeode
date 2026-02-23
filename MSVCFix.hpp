#pragma once

// Fix para MSVC y nontype_functional (Geode beta.3)
// __builtin_memset no existe en MSVC, mapeamos a memset
#if defined(_MSC_VER) && !defined(__clang__)
    #include <cstring>
    #ifndef __builtin_memset
        #define __builtin_memset(dst, val, size) std::memset(dst, val, size)
    #endif
#endif

