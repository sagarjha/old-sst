#pragma once
#include <array>
#include <functional>
#include <memory>
#include <type_traits>
#include <cassert>
#include <tuple>
#include "args-finder.hpp"
#include "combinator_utils.h"

//START declarations

namespace sst{
	template<typename Row,typename ExtensionList,typename NameEnum, NameEnum Name>
	struct PredicateBuilder;
	
		namespace predicate_builder {

		template<typename Row, typename ExtensionList,typename NameEnum, NameEnum Name>
		PredicateBuilder<Row,typename ExtensionList::template append<bool>, NameEnum, Name>
		E(const PredicateBuilder<Row, ExtensionList, NameEnum, Name>& pb);
			
			//F is a function of Row -> bool.  This will deduce that.
		template<typename NameEnum, NameEnum Name, typename F>
		auto as_row_pred_f(const F &f){
			using namespace util;
			using F2 = std::decay_t<decltype(convert(f))>;
			using undecayed_row = typename function_traits<F2>::template arg<0>::type;
			using Row = std::decay_t<undecayed_row>;
			using Entry = std::result_of_t<F(undecayed_row)>;
			using pred_builder = PredicateBuilder<Row,TypeList<Entry>,NameEnum, Name>;
			using Row_Extension = typename pred_builder::Row_Extension;
			return pred_builder{
				[f](const volatile Row &r, const volatile Row_Extension&, int zero){
					assert(zero == 0); return f(r);},std::tuple<>{}};
		}

#define as_row_pred(name, f...) ::sst::predicate_builder::as_row_pred_f<decltype(name),name>(f)

	}
}

///Create a way of referencing previous predicates. 


//END declarations, these are implementations 
namespace sst {

	using util::TypeList;

/**
 * Combinators for SST predicates.  These combinators can be used to define
 * new predicates using a simple logic language consisting of conjuction,
 * disjunction, integral-type comparison, and knowledge operators.
 */
	
	template<typename Row,typename ExtensionList,typename NameEnum, NameEnum Name>
	struct PredicateBuilder;

	template<typename Row,typename Entry, typename NameEnum, NameEnum Name>
	struct PredicateBuilder<Row,TypeList<Entry>,NameEnum, Name> {
		static_assert(std::is_pod<Row>::value,"Error: POD rows required!");
		struct Row_Extension{};
		using updater_functions_t = std::tuple<>;
		using row_extension_ptrs_t = std::tuple<>;
		const std::function<Entry (volatile const Row&, volatile const Row_Extension&, int)> curr_pred;
		const updater_functions_t updater_functions;
		using num_updater_functions = typename std::integral_constant<std::size_t, 0>::type;
	};


	template<typename, typename...> struct NamedFunctionTuples;
	template<typename ...> struct NamedRowPredicates;
	//this is an immutable struct
	template<typename Row, typename ExtensionList, typename NameEnum, NameEnum Name>
	struct PredicateBuilder {

		using row_entry = typename ExtensionList::hd;
		using tl = typename ExtensionList::tl;
		
		struct Row_Extension : public PredicateBuilder<Row,tl,NameEnum, Name>::Row_Extension {
			row_entry stored;
		};
		
		typedef std::function<void (volatile Row_Extension&, std::function<util::ref_pair<volatile Row, volatile Row_Extension> (int)>, const int num_rows)> updater_function_t;

		using old_updater_functions_t = typename PredicateBuilder<Row,tl, NameEnum, Name>::updater_functions_t;
		using old_row_extension_ptrs_t = typename PredicateBuilder<Row,tl, NameEnum, Name>::row_extension_ptrs_t;
		using updater_functions_t =
			std::decay_t<
			decltype(std::tuple_cat(
						 std::declval<old_updater_functions_t>(),
						 std::declval<std::tuple<updater_function_t> >()))>;
		
		using row_extension_ptrs_t =
			std::decay_t<
			decltype(std::tuple_cat(
						 std::declval<old_row_extension_ptrs_t>(),
						 std::declval<std::tuple<Row_Extension*> >()))>;

		using num_updater_functions = std::integral_constant<std::size_t, PredicateBuilder<Row,tl,NameEnum,Name>::num_updater_functions::value + 1>;
			
		const updater_functions_t updater_functions;
		
		const std::function<row_entry (volatile const Row&, volatile const Row_Extension&, int)> curr_pred;

		PredicateBuilder(const old_updater_functions_t & ufa, const updater_function_t &f, const decltype(curr_pred) curr_pred):
			updater_functions(std::tuple_cat(ufa,std::make_tuple(f))),curr_pred(curr_pred){}

		//for convenience when parameterizing SST
		using NamedRowPredicatesTypePack = NamedRowPredicates<PredicateBuilder>;
		using NamedFunctionTypePack = NamedFunctionTuples<void>;
	};

	namespace predicate_builder {

		template<typename Row, typename ExtensionList,typename NameEnum, NameEnum Name>
		PredicateBuilder<Row,typename ExtensionList::template append<bool>, NameEnum, Name>
		E(const PredicateBuilder<Row, ExtensionList, NameEnum, Name>& pb) {
			
			using next_builder = PredicateBuilder<Row,typename ExtensionList::template append<bool>, NameEnum, Name>;
			using Row_Extension = typename next_builder::Row_Extension;

			auto curr_pred = pb.curr_pred;
			
			return next_builder{
				pb.updater_functions,
					[curr_pred]
					(volatile Row_Extension& my_row, std::function<util::ref_pair<volatile Row,volatile Row_Extension> (int)> lookup_row, const int num_rows){
					bool result = true;
					for (int i = 0; i < num_rows; ++i){
						auto rowpair = lookup_row(i);
						if (!curr_pred(rowpair.l, rowpair.r,0)) result = false;
						}
					my_row.stored = result;
				},
					[curr_pred](volatile const Row& rw, volatile const Row_Extension &r, int prev){
						if (prev == 0) return r.stored;
						else return curr_pred(rw,r,prev-1);
					}
			};
		}

		
		template<typename> struct extract_row_str;
		template<typename Row, typename ExtensionList, typename NameEnum, NameEnum Name>
			struct extract_row_str<PredicateBuilder<Row,ExtensionList, NameEnum, Name> >{
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
