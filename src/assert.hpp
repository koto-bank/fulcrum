#pragma once

#include <cassert>

#if defined(DEBUG)
#define fc_assert(e) (assert(e))
#else
#define fc_assert(e) ((void)0)
#endif
