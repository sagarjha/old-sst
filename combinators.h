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
	
	template<typename Row,std::size_t,int uniqueness_tag>
	struct PredicateBuilder;

	template<typename Row,int uniqueness_tag>
	struct PredicateBuilder<Row,0,uniqueness_tag> {
		static_assert(std::is_pod<Row>::value,"Error: POD rows required!");
		struct Row_Extension{};
		using updater_functions_t = std::tuple<>;
		const std::function<bool (const Row, const Row_Extension&)> curr_pred;
		const updater_functions_t updater_functions;
	};
													   
	//this is an immutable struct
	template<typename Row, std::size_t num_stored_bools,int uniqueness_tag>
	struct PredicateBuilder {
		
		struct Row_Extension : public PredicateBuilder<Row,num_stored_bools - 1,uniqueness_tag>::Row_Extension {
			bool stored;
		};
		
		typedef std::function<void (Row_Extension&, std::function<util::ref_pair<Row, Row_Extension> (int)>, const int num_rows)> updater_function_t;

		using old_updater_functions_t = typename PredicateBuilder<Row,num_stored_bools - 1, uniqueness_tag>::updater_functions_t;
		using updater_functions_t =
			std::decay_t<
			decltype(std::tuple_cat(
						 std::declval<old_updater_functions_t>(),
						 std::declval<std::tuple<updater_function_t> >()))>;

		using num_updater_functions = typename std::integral_constant<std::size_t, num_stored_bools>::type;
			
		const updater_functions_t updater_functions;
		
		const std::function<bool (const Row&, const Row_Extension&)> curr_pred;

		PredicateBuilder(const old_updater_functions_t & ufa, const updater_function_t &f, const decltype(curr_pred) curr_pred):
			updater_functions(std::tuple_cat(ufa,std::make_tuple(f))),curr_pred(curr_pred){}
	};

	namespace predicate_builder {

		template<typename Row, std::size_t num_stored_bools,int uniqueness_tag>
		auto E(const PredicateBuilder<Row, num_stored_bools, uniqueness_tag>& pb)  {
			
			using next_builder = PredicateBuilder<Row,num_stored_bools + 1, uniqueness_tag>;
			using Row_Extension = typename next_builder::Row_Extension;

			auto curr_pred = pb.curr_pred;
			
			return next_builder{
				pb.updater_functions,
					[curr_pred]
					(Row_Extension& my_row, std::function<util::ref_pair<Row,Row_Extension> (int)> lookup_row, const int num_rows){
						bool result = true;
						for (int i = 0; i < num_rows; ++i){
							auto rowpair = lookup_row(i);
							if (!curr_pred(rowpair.l, rowpair.r)) result = false;
						}
						my_row.stored = result;
					},
						[](const Row&, const Row_Extension &r){
							return r.stored;
						}
						};
			}

			//F is a function of Row -> bool.  This will deduce that.
		template<int uniqueness_tag, typename F>
		auto E(F f){
			using namespace util;
			auto f2 = convert(f);
			using Row = std::decay_t<typename function_traits<decltype(f2)>::template arg<0>::type>;
			using pred_builder = PredicateBuilder<Row,0,uniqueness_tag>;
			using Row_Extension = typename pred_builder::Row_Extension;
			return E(pred_builder{[f](const Row &r, const Row_Extension&){return f(r);},std::tuple<>{}});
		}

		
		template<typename> struct extract_row_str;
		template<typename Row, std::size_t count, int uniqueness_tag>
			struct extract_row_str<PredicateBuilder<Row,count, uniqueness_tag> >{
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
