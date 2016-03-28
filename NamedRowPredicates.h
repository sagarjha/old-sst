#pragma once
#include "combinators.h"

namespace sst{
	
	constexpr bool all_predicate_builders(){
		return true;
	}
	
	template<typename Row, std::size_t count, int uniqueness_tag, typename... Rst>
	constexpr bool all_predicate_builders(PredicateBuilder<Row,count, uniqueness_tag> const * const pb, Rst const * const ... rst){
		static_assert(std::is_pod<Row>::value,"Error: Predicate Builders need POD rows!");
		return all_predicate_builders(rst...);
	}
	
	template<typename ...>
	struct NamedRowPredicates;

	template<>
	struct NamedRowPredicates<>{
		using is_tail = std::true_type;
		using function_types = std::tuple<>;
	};
	
	template<typename PB, typename ... PredBuilders>
	struct NamedRowPredicates<PB, PredBuilders...> {
		static_assert(all_predicate_builders(util::mke_p<PB>()), "Error: this parameter pack must be of predicate builders!");
		static_assert(all_predicate_builders(util::mke_p<PredBuilders>()...), "Error: this parameter pack must be of predicate builders!");
		using is_tail = std::false_type;
		using function_types = std::tuple<PB, PredBuilders ...>;
		using hd = PB;
		using rst = NamedRowPredicates<PredBuilders...>;
	};
}
