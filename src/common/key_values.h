#pragma once

kv::zhlt_detaillevel(mapent).get();
kv::zhlt_detaillevel(mapent).set();

kv::classname(mapent).is(u8"treter");

kv::zhlt_detaillevel.in(mapent);
kv::zhlt_detaillevel.set(mapent, 54);

constexpr zhlt_detaillevel = numeric_key_value<detail_level>(
	mapent, u8"zhlt_detaillevel"
);
