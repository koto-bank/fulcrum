#pragma once

#include <type_traits>

template<typename T>
concept IsLongInteger = std::same_as<T, uint64_t> || std::same_as<T, int64_t>;
