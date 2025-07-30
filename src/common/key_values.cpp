#include "key_values.h"

entity_key_value const *
find_key_value(entity_t const & ent, std::u8string_view key) noexcept {
	auto it = std::ranges::find_if(
		ent.keyValues,
		[&key](entity_key_value const & kv) { return kv.key() == key; }
	);
	if (it == ent.keyValues.end()) {
		return nullptr;
	}
	return &*it;
}

entity_key_value*
find_key_value(entity_t& ent, std::u8string_view key) noexcept {
	entity_t const & constEnt{ ent };
	return const_cast<entity_key_value*>(find_key_value(constEnt, key));
}

entity_key_value& find_key_value_or_new_element_for_overwriting(
	entity_t& ent, std::u8string_view key
) noexcept {
	entity_key_value* removed{ nullptr };
	for (entity_key_value& kv : ent.keyValues) {
		if (kv.key() == key) {
			return kv;
		}
		if (kv.is_removed() && !removed) {
			removed = &kv;
		}
	}
	if (removed) {
		return *removed;
	}

	return ent.keyValues.emplace_back();
}

void remove_key_value(entity_t& ent, std::u8string_view key) noexcept {
	entity_key_value* const kv{ find_key_value(ent, key) };
	if (kv) {
		kv->remove();
	}
}

void replace_key_value(entity_t& ent, entity_key_value keyValue) {
	find_key_value_or_new_element_for_overwriting(
		ent, keyValue.key()
	) = keyValue;
}
