#pragma once
#include <array>
#include <functional>
#include <memory>
#include <type_traits>
#include <cassert>
#include "args-finder.hpp"


namespace sst {


/**
 * Combinators for SST predicates.  These combinators can be used to define
 * new predicates using a simple logic language consisting of conjuction,
 * disjunction, integral-type comparison, and knowledge operators.
 */

	template<typename Row>
	struct Row_Extension : public Row{
		//this is an array
		bool *stored;
		std::size_t sizeof_stored;
		//BUYER BEWARE:  SST will need to delete the array here.
		//Row_Extension must remain trivially copyable.
	};
	
	template<typename Row>
	using  = ;
								
	template<typename Row,std::size_t>
	struct PredicateBuilder;
	
	template<typename Row>
	struct PredicateBuilder<Row,50>{
		typedef std::function<void (Row_Extension<Row>&, std::function<const Row_Extension<Row>& (int)>, const int num_rows)> updater_function_t;
		using updater_function_array_t =
			const std::array<updater_function_t<Row>,50>;
	};
													   
	//this is an immutable struct
	template<typename Row, std::size_t num_stored_bools>
	struct PredicateBuilder {

		using _Row_Extension = Row_Extension<Row>;
		using _updater_function_t = updater_function_t<Row>;
		
		using updater_function_array_t = _updater_function_t[num_stored_bools];
			
		const updater_function_array_t updater_functions;
		
		const std::function<bool (const _Row_Extension& r)> curr_pred;

		PredicateBuilder(const updater_function_array_t & ufa, const decltype(curr_pred) curr_pred):
			curr_pred(curr_pred){
			auto uf_hndle = const_cast<_updater_function_t*>(updater_functions);
			for (int i = 0; i < num_stored_bools; ++i){
				uf_hndle[i] = ufa[i];
			}
		}
		
		auto E() const {
			
			using next_builder = PredicateBuilder<Row,num_stored_bools + 1>;
			using next_function_array_t = typename next_builder::updater_function_array_t;
			next_function_array_t next_funs;
			
			for (int i = 0; i < num_stored_bools; ++i){
				next_funs[i] = updater_functions[i];
			}
			
			next_funs[num_stored_bools] = [curr_pred = this->curr_pred]
				(_Row_Extension& my_row, std::function<const _Row_Extension& (int)> lookup_row, const int num_rows){
				bool result = true;
				for (int i = 0; i < num_rows; ++i){
					if (!curr_pred(lookup_row(i))) result = false;
				}
				assert(my_row.sizeof_stored == num_stored_bools + 1);
				my_row.stored[num_stored_bools] = result;
			};
			
			return next_builder{
				next_funs,
					[](const _Row_Extension &r){
					return r.stored[num_stored_bools];
				}
			};}
	};
						  
	namespace predicate_builder {
		template<typename F>
		auto E(F f){
			using namespace util;
			using Row = std::decay_t<typename function_traits<F>::template arg<0>::type>;
			using pred_builder = PredicateBuilder<Row,1>;
			using Row_Extension = Row_Extension<Row>;
			return pred_builder{{{}},[f](const Row_Extension &r){return f(r);}}.E();
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
