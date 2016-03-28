#pragma once
#include <array>
#include <functional>
#include <memory>
#include <type_traits>
#include <cassert>
#include <tuple>
#include "args-finder.hpp"


namespace sst {


/**
 * Combinators for SST predicates.  These combinators can be used to define
 * new predicates using a simple logic language consisting of conjuction,
 * disjunction, integral-type comparison, and knowledge operators.
 */
	
	template<typename Row,std::size_t>
	struct PredicateBuilder;

	template<typename Row>
	struct PredicateBuilder<Row,0> {
		using Row_Extension = Row;
		using updater_functions_t = std::tuple<>;
		const std::function<bool (const Row_Extension& r)> curr_pred;
		const updater_functions_t updater_functions;
	};
													   
	//this is an immutable struct
	template<typename Row, std::size_t num_stored_bools>
	struct PredicateBuilder {
		
		struct Row_Extension : public PredicateBuilder<Row,num_stored_bools - 1>::Row_Extension {
			bool stored;
		};
		
		typedef std::function<void (Row_Extension&, std::function<const Row_Extension& (int)>, const int num_rows)> updater_function_t;

		using old_updater_functions_t = typename PredicateBuilder<Row,num_stored_bools - 1>::updater_functions_t;
		using updater_functions_t =
			std::decay_t<
			decltype(std::tuple_cat(
						 std::declval<old_updater_functions_t>(),
						 std::declval<std::tuple<updater_function_t> >()))>;
			
		const updater_functions_t updater_functions;
		
		const std::function<bool (const Row_Extension& r)> curr_pred;

		PredicateBuilder(const old_updater_functions_t & ufa, const updater_function_t &f, const decltype(curr_pred) curr_pred):
			updater_functions(std::tuple_cat(ufa,std::make_tuple(f))),curr_pred(curr_pred){}
	};

	namespace predicate_builder {

		template<typename Row, std::size_t num_stored_bools>
		auto E(const PredicateBuilder<Row, num_stored_bools>& pb)  {
			
			using next_builder = PredicateBuilder<Row,num_stored_bools + 1>;
			using Row_Extension = typename next_builder::Row_Extension;

			auto curr_pred = pb.curr_pred;
			
			return next_builder{
				pb.updater_functions,
					[curr_pred]
					(Row_Extension& my_row, std::function<const Row_Extension& (int)> lookup_row, const int num_rows){
						bool result = true;
						for (int i = 0; i < num_rows; ++i){
							if (!curr_pred(lookup_row(i))) result = false;
						}
						my_row.stored = result;
					},
						[](const Row_Extension &r){
							return r.stored;
						}
						};
			}
		
		template<typename F>
		auto E(F f){
			using namespace util;
			auto f2 = convert(f);
			using Row = std::decay_t<typename function_traits<decltype(f2)>::template arg<0>::type>;
			using pred_builder = PredicateBuilder<Row,0>;
			return E(pred_builder{f,std::tuple<>{}});
		}
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
