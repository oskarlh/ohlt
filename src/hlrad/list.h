#pragma once

/*
 *  A generic template list class.
 *  Fairly typical of the list example you would
 *  find in any c++ book.
 */
// used by progmesh //seedee

#include <assert.h>
#include <stdio.h>

template <class Type>
class List final {
  public:
	List(int s = 0);
	~List();
	void allocate(int s);
	void SetSize(int s);
	void Pack();
	void Add(Type);
	void AddUnique(Type);
	int Contains(Type);
	void Remove(Type);
	void DelIndex(int i);
	Type* element;
	int num;
	int array_size;

	Type& operator[](int i) {
		assert(i >= 0 && i < num);
		return element[i];
	}
};

template <class Type>
List<Type>::List(int s) {
	num = 0;
	array_size = 0;
	element = nullptr;
	if (s) {
		allocate(s);
	}
}

template <class Type>
List<Type>::~List() {
	delete element;
}

template <class Type>
void List<Type>::allocate(int s) {
	assert(s > 0);
	assert(s >= num);
	Type* old = element;
	array_size = s;
	element = new Type[array_size]{};
	assert(element);

	for (int i = 0; i < num; i++) {
		element[i] = old[i];
	}

	if (old) {
		delete old;
	}
}

template <class Type>
void List<Type>::SetSize(int s) {
	if (s == 0) {
		if (element) {
			delete element;
		}
	} else {
		allocate(s);
	}

	num = s;
}

template <class Type>
void List<Type>::Pack() {
	allocate(num);
}

template <class Type>
void List<Type>::Add(Type t) {
	assert(num <= array_size);

	if (num == array_size) {
		allocate((array_size) ? array_size * 2 : 16);
	}

	element[num++] = t;
}

template <class Type>
int List<Type>::Contains(Type t) {
	int count = 0;

	for (int i = 0; i < num; i++) {
		if (element[i] == t) {
			count++;
		}
	}

	return count;
}

template <class Type>
void List<Type>::AddUnique(Type t) {
	if (!Contains(t)) {
		Add(t);
	}
}

template <class Type>
void List<Type>::DelIndex(int i) {
	assert(i < num);
	num--;

	while (i < num) {
		element[i] = element[i + 1];
		i++;
	}
}

template <class Type>
void List<Type>::Remove(Type t) {
	int i;

	for (i = 0; i < num; i++) {
		if (element[i] == t) {
			break;
		}
	}

	DelIndex(i);

	for (i = 0; i < num; i++) {
		assert(element[i] != t);
	}
}
