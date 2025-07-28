#pragma once

/*
 *  A generic template list class.
 *  Fairly typical of the list example you would
 *  find in any c++ book.
 */
// used by progmesh

#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <vector>

template <class Type>
class List final {
  public:
	List(std::size_t s = 0);
	void SetSize(std::size_t s);
	void Add(Type);
	void AddUnique(Type);
	std::size_t Contains(Type const &);
	void Remove(Type const &);
	void DelIndex(std::size_t i);
	std::vector<Type> storage;

	std::size_t Size() {
		return storage.size();
	}

	Type& operator[](std::size_t i) {
		return storage[i];
	}
};

template <class Type>
List<Type>::List(std::size_t s) {
	storage.reserve(s);
}

template <class Type>
void List<Type>::SetSize(std::size_t s) {
	storage.resize(s);
}

template <class Type>
void List<Type>::Add(Type t) {
	storage.emplace_back(t);
}

template <class Type>
std::size_t List<Type>::Contains(Type const & t) {
	return std::ranges::count(storage, t);
}

template <class Type>
void List<Type>::AddUnique(Type t) {
	if (!Contains(t)) {
		Add(t);
	}
}

template <class Type>
void List<Type>::DelIndex(std::size_t i) {
	storage.erase(storage.begin() + i);
}

template <class Type>
void List<Type>::Remove(Type const & t) {
	auto it = std::ranges::find(storage, t);
	assert(it != storage.end());
	storage.erase(it);
}
