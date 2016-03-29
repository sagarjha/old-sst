#pragma once
#include <array>
#include <functional>
#include <memory>
#include <type_traits>
#include <cassert>
#include <tuple>
#include "args-finder.hpp"
#include "combinator_utils.h"


namespace sst {


/**
 * Combinators for SST predicates.  These combinators can be used to define
 * new predicates using a simple logic language consisting of conjuction,
 * disjunction, integral-type comparison, and knowledge operators.
 */
	
	template<typename Row,std::size_t,typename NameEnum, NameEnum Name>
	struct PredicateBuilder;

	template<typename Row,typename NameEnum, NameEnum Name>
	struct PredicateBuilder<Row,0,NameEnum, Name> {
		static_assert(std::is_pod<Row>::value,"Error: POD rows required!");
		struct Row_Extension{};
		using updater_functions_t = std::tuple<>;
		const std::function<bool (volatile const Row&, volatile const Row_Extension&)> curr_pred;
		const updater_functions_t updater_functions;
	};
													   
	//this is an immutable struct
	template<typename Row, std::size_t num_stored_bools,typename NameEnum, NameEnum Name>
	struct PredicateBuilder {
		
		struct Row_Extension : public PredicateBuilder<Row,num_stored_bools - 1,NameEnum, Name>::Row_Extension {
			bool stored;
		};
		
		typedef std::function<void (volatile Row_Extension&, std::function<util::ref_pair<volatile Row, volatile Row_Extension> (int)>, const int num_rows)> updater_function_t;

		using old_updater_functions_t = typename PredicateBuilder<Row,num_stored_bools - 1, NameEnum, Name>::updater_functions_t;
		using updater_functions_t =
			std::decay_t<
			decltype(std::tuple_cat(
						 std::declval<old_updater_functions_t>(),
						 std::declval<std::tuple<updater_function_t> >()))>;

		using num_updater_functions = typename std::integral_constant<std::size_t, num_stored_bools>::type;
			
		const updater_functions_t updater_functions;
		
		const std::function<bool (volatile const Row&, volatile const Row_Extension&)> curr_pred;

		PredicateBuilder(const old_updater_functions_t & ufa, const updater_function_t &f, const decltype(curr_pred) curr_pred):
			updater_functions(std::tuple_cat(ufa,std::make_tuple(f))),curr_pred(curr_pred){}
	};

	namespace predicate_builder {

		template<typename Row, std::size_t num_stored_bools,typename NameEnum, NameEnum Name>
		auto E(const PredicateBuilder<Row, num_stored_bools, NameEnum, Name>& pb)  {
			
			using next_builder = PredicateBuilder<Row,num_stored_bools + 1, NameEnum, Name>;
			using Row_Extension = typename next_builder::Row_Extension;

			auto curr_pred = pb.curr_pred;

								
			
			return next_builder{
				pb.updater_functions,
					[curr_pred]
					(volatile Row_Extension& my_row, std::function<util::ref_pair<volatile Row,volatile Row_Extension> (int)> lookup_row, const int num_rows){
					bool result = true;
					for (int i = 0; i < num_rows; ++i){
						auto rowpair = lookup_row(i);
						if (!curr_pred(rowpair.l, rowpair.r)) result = false;
						}
					my_row.stored = result;
				},
					[](volatile const Row&, volatile const Row_Extension &r){
						return r.stored;
					}	
			};
		}

			//F is a function of Row -> bool.  This will deduce that.
		template<typename NameEnum, NameEnum Name, typename F>
		auto as_row_pred_f(const F &f){
			using namespace util;
			using F2 = std::decay_t<decltype(convert(f))>;
			using Row = std::decay_t<typename function_traits<F2>::template arg<0>::type>;
			using pred_builder = PredicateBuilder<Row,0,NameEnum, Name>;
			using Row_Extension = typename pred_builder::Row_Extension;
			return E(pred_builder{[f](const volatile Row &r, const volatile Row_Extension&){return f(r);},std::tuple<>{}});
		}

#define as_row_pred(name, f...) ::sst::predicate_builder::as_row_pred_f<decltype(name),name>(f)

		
		template<typename> struct extract_row_str;
		template<typename Row, std::size_t count, typename NameEnum, NameEnum Name>
			struct extract_row_str<PredicateBuilder<Row,count, NameEnum, Name> >{
			using type = Row;
		};

		template<typename T>
			using extract_row = typename extract_row_str<T>::type;
	}
}



/*

So the idea is to have an SST builder which takes expressions in a constructor.

Something like build_sst(arguments, associate_name(foo, E (E ([](){row.k > row.val}))) )

E ( E (...) ) would have to produce a structure with these components: 
 - extra spaces in row they rely on
 - function(s) to populate those extra spaces
 - function to associate with foo
 - we *don't* give foo a space. Foo is just a name. 

 */
