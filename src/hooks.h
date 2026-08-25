// dbz3 - ReXGlue codegen bug workarounds (see hooks.cpp for details).
// Only applies to the US/NA build: the EU/PAL build has different guest
// addresses and excludes hooks.cpp (see CMakeLists DBZ3_GENERATED_DIR).
#pragma once

#ifndef DBZ3_EU_VARIANT

#include <rex/ppc/context.h>
#include <rex/ppc/function.h>

// These externs are provided by the recompiled codegen output (generated/).
REX_EXTERN(sub_820E54F8);
REX_EXTERN(sub_820E0560);

#endif // !DBZ3_EU_VARIANT
