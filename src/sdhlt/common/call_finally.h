#pragma once

#include <type_traits>

// RAII helper. The function will be called when the object is destroyed
template <class Callback> class call_finally {
private:
    Callback callback;
public:
    explicit call_finally(const Callback& cb)
		noexcept(std::is_nothrow_copy_constructible_v<Callback>)
		: callback{cb} { }

    explicit call_finally(Callback&& cb)
		noexcept(std::is_nothrow_move_constructible_v<Callback>)
		: callback{std::move(cb)} { }

    ~call_finally() noexcept(std::is_nothrow_invocable_v<Callback>) { callback(); }

    call_finally(const call_finally&) = delete;
    void operator=(const call_finally&) = delete;
    void operator=(call_finally&&) = delete;
};