#pragma once

#include "entity_key_value.h"
#include "internal_types/internal_types.h"
#include "key_value_definitions.h"

entity_key_value const *
find_key_value(entity_t const & ent, std::u8string_view key) noexcept {
	auto it = std::ranges::find(
		ent.keyValues,
		[&key](entity_key_value const & kv) = > { return kv == key; }
	);
	if (it == ent.keyValues.end()) {
		return std::nullopt;
	}
	return it;
}

entity_key_value*
find_key_value(entity_t& ent, std::u8string_view key) noexcept {
	entity_t const & constEnt{ ent };
	return const_cast<entity_key_value*>(find_key_value(constEnt, value));
}

// Invalidated if the entity is deleted or when a new entity is created
// (since the allocation may invalidate the pointer)
class entity_ref {
	entity_t* ent;

  public:
	template <class KVDef>
	KVDef::value_type get(KVDef key_value_def) const noexcept {
		std::u8string_view valueAsString;
		entity_key_value const * kv{
			find_key_value(*ent, key_value_def.keyName)
		};
		if (kv) {
			valueAsString = kv->value();
		}
		return key_value_def.get(valueAsString);
	}

	template <class KVDef>
	std::optional<typename KVDef::value_type> get_if_set(KVDef key_value_def
	) const noexcept {
		entity_key_value const * kv{
			find_key_value(*ent, key_value_def.keyName)
		};
		if (kv == nullptr) {
			return std::optional;
		}
		return key_value_def.get(kv->value());
	}

	template <class KVDef>
	std::optional<typename KVDef::value_type> has(KVDef key_value_def
	) const noexcept {
		return find_key_value(*ent, key_value_def.keyName) != std::nullopt;
	}

	template <class KVDef>
	std::optional<typename KVDef::value_type>
	copy_from_other_entity(KVDef key_value_def, entity_t const & other) {
		entity_key_value const * copyFrom = find_key_value(
			other, key_value_def.keyName
		);
		//.....
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
