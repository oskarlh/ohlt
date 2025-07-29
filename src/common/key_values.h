#pragma once

#include "entity_key_value.h"
#include "internal_types/internal_types.h"
#include "key_value_definitions.h"

#include <algorithm>
#include <optional>

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

// Invalidated if the entity is deleted or when a new entity is created
// (since the allocation may relocate already allocated entities)
class entity_ref {
  private:
	entity_t* ent;

  public:
	template <class KVDef>
	KVDef::value_type get(KVDef keyValueDef) const noexcept {
		entity_key_value const * kv{
			find_key_value(*ent, keyValueDef.keyName)
		};
		std::u8string_view valueAsString;
		if (kv) {
			valueAsString = kv->value();
		}
		return keyValueDef.get(valueAsString);
	}

	template <class KVDef>
	bool
	is(KVDef keyValueDef, KVDef::value_type const & value) const noexcept {
		return get(keyValueDef) == value;
	}

	template <class KVDef>
	std::optional<typename KVDef::value_type> get_if_set(KVDef keyValueDef
	) const noexcept {
		entity_key_value const * kv{
			find_key_value(*ent, keyValueDef.keyName)
		};
		if (kv == nullptr) {
			return std::nullopt;
		}
		return keyValueDef.get(kv->value());
	}

	template <class KVDef>
	std::optional<typename KVDef::value_type> has(KVDef keyValueDef
	) const noexcept {
		return find_key_value(*ent, keyValueDef.keyName) != std::nullopt;
	}

	template <class KVDef>
	std::optional<typename KVDef::value_type>
	copy_from_other_entity(KVDef keyValueDef, entity_t const & other) {
		entity_key_value const * copyFrom = find_key_value(
			other, keyValueDef.keyName
		);
		if (copyFrom) {
			replace_key_value(*ent, *copyFrom);
		} else {
			remove_key_value(*ent, keyValueDef.keyName);
		}
	}

	template <class KVDef>
	void set(KVDef keyValueDef, KVDef::value_type const & newValue) {
		entity_key_value newKeyValue;
		keyValueDef.set(newKeyValue, newValue);

		if (!newKeyValue.is_removed()) [[likely]] {
			replace_key_value(*ent, std::move(newValue));
		} else {
			remove_key_value(*ent, keyValueDef.keyName);
		}
	}

	template <class KVDef>
	void remove(KVDef keyValueDef) noexcept {
		remove_key_value(*ent, keyValueDef.keyName);
	}
};

namespace kv {
	static constexpr auto classname{ string_key_value_def{
		u8"classname" } };

	static constexpr auto origin{ vec3_key_value_def<float>{ u8"origin" } };

	static constexpr auto zhlt_chopdown{
		integer_key_value_def<detail_level>{ u8"zhlt_chopdown" }
	};

	static constexpr auto zhlt_chopup{ integer_key_value_def<detail_level>{
		u8"zhlt_chopup" } };

	static constexpr auto zhlt_clipnodedetaillevel{
		integer_key_value_def<detail_level>{ u8"zhlt_clipnodedetaillevel" }
	};

	static constexpr auto zhlt_coplanarpriority{
		integer_key_value_def<coplanar_priority>{
			u8"zhlt_coplanarpriority" }
	};

	static constexpr auto zhlt_detaillevel{
		integer_key_value_def<detail_level>{ u8"zhlt_detaillevel" }
	};
} // namespace kv
