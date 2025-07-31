#pragma once

#include <type_traits>
#include <utility>

// RAII helper. The function will be called when the object is destroyed
template <class Callback>
class call_finally final {
  private:
	Callback callback;

  public:
	explicit call_finally(Callback const & cb)
		noexcept(std::is_nothrow_copy_constructible_v<Callback>) :
		callback{ cb } { }

	explicit call_finally(Callback&& cb)
		noexcept(std::is_nothrow_move_constructible_v<Callback>) :
		callback{ std::move(cb) } { }

	~call_finally() noexcept(std::is_nothrow_invocable_v<Callback>) {
		callback();
	}

	call_finally(call_finally const &) = delete;
	void operator=(call_finally const &) = delete;
	void operator=(call_finally&&) = delete;
};
