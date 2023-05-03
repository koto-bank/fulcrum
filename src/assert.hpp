#pragma once

#include <cassert>

#if defined(FC_DEBUG)
#define fc_assert(e) (assert(e))
#else
#define fc_assert(e) ((void)0)
#endif
