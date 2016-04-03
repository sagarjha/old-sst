#pragma once
#include "combinators.h"

namespace sst{
	
	constexpr bool all_predicate_builders(){
		return true;
	}
	
	template<typename Row, std::size_t count, typename NameEnum, NameEnum Name, typename... Rst>
	constexpr bool all_predicate_builders(PredicateBuilder<Row,count, NameEnum, Name> const * const pb, Rst const * const ... rst){
		static_assert(std::is_pod<Row>::value,"Error: Predicate Builders need POD rows!");
		return all_predicate_builders(rst...);
	}
	
	template<typename ...>
	struct NamedRowPredicates;

	template<>
	struct NamedRowPredicates<>{
		using is_tail = std::true_type;
		using predicate_types = std::tuple<>;
		using row_types = std::tuple<>;
		using getter_types = std::tuple<>;
		using size = typename std::integral_constant<std::size_t, 0>::type;
	};
	
	template<typename PB, typename ... PredBuilders>
	struct NamedRowPredicates<PB, PredBuilders...> {
		static_assert(all_predicate_builders(util::mke_p<PB>()), "Error: this parameter pack must be of predicate builders!");
		static_assert(all_predicate_builders(util::mke_p<PredBuilders>()...), "Error: this parameter pack must be of predicate builders!");
		using is_tail = std::false_type;
		using predicate_types = std::tuple<PB, PredBuilders ...>;
		using row_types = std::tuple<typename PB::Row_Extension, typename PredBuilders::Row_Extension...>;
		using getter_types = std::tuple<std::decay_t<decltype(PB::curr_pred)>, std::decay_t<decltype(PredBuilders::curr_pred)>...>;
		using hd = PB;
		using rst = NamedRowPredicates<PredBuilders...>;
		using size = typename std::integral_constant<std::size_t, sizeof...(PredBuilders) + 1>::type;
		using num_updater_functions =
			typename std::integral_constant<
			std::size_t,
			util::sum(PB::num_updater_functions::value, PredBuilders::num_updater_functions::value...)>::type;

		using NamedRowPredicatesTypePack = NamedRowPredicates;
		using NamedFunctionTypePack = NamedFunctionTuples<void>;
	};
}
