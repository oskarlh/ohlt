#pragma once

#include "call_finally.h"
#include "utf8.h"
#include "vector_inplace.h"

#include <variant>

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>;
template<class var_t, class... Func> auto visit_with(var_t && variant, Func &&... funcs)
{
	return std::visit(overload{ funcs... }, variant);
}
