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

	using predicate_builder::TypeList;

	template<typename Row,typename Metadata>
	struct PredicateBuilder<Row,TypeList<Metadata> > {

		using row_entry = typename Metadata::ExtensionType;
		
		template<typename T>
		using append = typename TypeList<Metadata>::template append<T>;
		
		static_assert(std::is_pod<Row>::value,"Error: POD rows required!");
		struct Row_Extension{};
		
		const typename Metadata::raw_getter curr_pred_raw;
		const std::function<row_entry (volatile const Row&, volatile const Row_Extension&)> curr_pred;
		const std::function<row_entry (volatile const Row&) > base;
		using num_updater_functions = typename std::integral_constant<std::size_t, 0>::type;

		template<typename T>
		using Getters = std::conditional_t<
			Metadata::has_name::value,
			std::tuple<std::function<row_entry (T)> >,
			std::tuple<> >;

		template<typename T>
		std::enable_if_t<Metadata::has_name::value, Getters<const volatile T&> > wrap_getters() const {
			std::function<row_entry (volatile const T&)> f
			{[curr_pred = this->curr_pred](volatile const T& t){
					return curr_pred(t,t);
				}};
			return std::make_tuple(f);
		}

		template<typename T>
		std::enable_if_t<!Metadata::has_name::value, Getters<const volatile T&> > wrap_getters() const {
			return std::tuple<>{};
		}
	};

	template<typename, typename...> struct NamedFunctionTuples;
	template<typename ...> struct NamedRowPredicates;
	//this is an immutable struct
	template<typename Row, typename ExtensionList>
	struct PredicateBuilder {
		
		template<typename T>
		using append = typename ExtensionList::template append<T>;

		using hd = typename ExtensionList::hd;
		using is_named = std::integral_constant<bool, (predicate_builder::choose_uniqueness_tag<hd>::value >= 0)>;

		static_assert(is_named::value ? predicate_builder::choose_uniqueness_tag<hd>::value >= 0 : true, "Internal Error: renaming failed!");
		
		using row_entry = typename hd::ExtensionType;
		using tl = typename ExtensionList::tl;
		
		struct Row_Extension : public PredicateBuilder<Row,tl>::Row_Extension {
			using super = typename PredicateBuilder<Row,tl>::Row_Extension;
			row_entry stored;
		};
		
		typedef std::function<void (volatile Row_Extension&, std::function<util::ref_pair<volatile Row, volatile Row_Extension> (int)>, const int num_rows)> updater_function_t;
		
		using num_updater_functions = std::integral_constant<std::size_t, PredicateBuilder<Row,tl>::num_updater_functions::value + 1>;

		template<typename T>
		using Getters = std::decay_t<
			decltype(std::tuple_cat(
						 std::declval<
						 std::conditional_t<hd::has_name::value,std::tuple<std::function<row_entry (T)> >, std::tuple<> >
						 >(),
						 std::declval<typename PredicateBuilder<Row,tl>::template Getters<T> >()))>;

		using num_getters = typename std::tuple_size<Getters<int> >::type;

		template<typename NameEnum>
		using name_enum_matches = predicate_builder::NameEnumMatches<NameEnum, ExtensionList>;
			
		const typename hd::raw_updater updater_function_raw;
		const updater_function_t updater_function;

		const PredicateBuilder<Row,tl> prev_preds;
		using pred_t = std::function<row_entry (volatile const Row&, volatile const Row_Extension&)>;
		const typename hd::raw_getter curr_pred_raw;
		const pred_t curr_pred;
		
		template<typename T>
		std::enable_if_t<hd::has_name::value, Getters<const volatile T&> > wrap_getters() const {
			std::function<row_entry (volatile const T&)> f
			{[curr_pred = this->curr_pred](volatile const T& t){
					return curr_pred(t,t);
				}};
			return std::tuple_cat(std::make_tuple(f),prev_preds.template wrap_getters<T>());
		}

		template<typename T>
		std::enable_if_t<!hd::has_name::value, Getters<const volatile T&> > wrap_getters() const {
			return prev_preds.template wrap_getters<T>();
		}

		PredicateBuilder(const PredicateBuilder<Row,tl> &prev, const decltype(updater_function_raw) &f, const decltype(curr_pred_raw) curr_pred):
			updater_function_raw(f),updater_function(f),prev_preds(prev),curr_pred_raw(curr_pred),curr_pred(curr_pred){}

		//for convenience when parameterizing SST
		using NamedRowPredicatesTypePack = NamedRowPredicates<PredicateBuilder>;
		using NamedFunctionTypePack = NamedFunctionTuples<void>;
	};

	namespace predicate_builder {

		//F is a function of Row -> bool.  This will deduce that.
		template<typename F>
		auto as_row_pred(const F &f){
			using namespace util;
			using F2 = std::decay_t<decltype(convert(f))>;
			using undecayed_row = typename function_traits<F2>::template arg<0>::type;
			using Row = std::decay_t<undecayed_row>;
			using Entry = std::result_of_t<F(undecayed_row)>;
			auto pred_f = [f](const volatile Row &r, const volatile auto&){
				return f(r);};
			using pred_builder = PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Entry,-1,void, decltype(pred_f)> > >;
			return pred_builder{pred_f,pred_f};
		}


		//we don't have a name yet,
		//but we're the tail.
		template<typename NameEnum, NameEnum Name, typename Row, typename Ext, typename Up, typename Get>
		auto name_predicate(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,-1,Up,Get> > > &pb){
			using This_list = TypeList< PredicateMetadata<NameEnum,Name,Ext,Up,Get> >;
			using next_builder = PredicateBuilder<Row, This_list>;
			return next_builder{pb.curr_pred_raw,pb.curr_pred_raw};
		}
		
		//nobody in this tree has a name yet;
		//we need to propogate a uniqueness-tag change
		//down the entire tree!
		template<typename NameEnum, NameEnum Name, typename Row, typename Ext, typename Up, typename Get, typename hd, typename... tl>
		auto name_predicate(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,-1,Up,Get>,hd,tl... > > &pb){
			constexpr int unique = static_cast<int>(Name);
			auto new_prev = change_uniqueness<unique>(pb.prev_preds);
			using This_list = typename decltype(new_prev)::template append <PredicateMetadata<NameEnum,Name,Ext,Up,Get> >;
			using next_builder = PredicateBuilder<Row, This_list>;
			return next_builder{new_prev,pb.updater_function_raw,pb.curr_pred_raw};
		}

		//something else in here has been named, so we can just name this PB and need not touch previous ones
		template<typename NameEnum, NameEnum Name, typename Row, typename Ext, typename Up, typename Get, int unique, typename... tl>
		auto name_predicate(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,unique,Up,Get>,tl... > > &pb){
			static_assert(unique >= 0, "Internal error: overload resolution fails");
			using next_builder = PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum,Name,Ext,Up,Get>,tl...> >;
			return next_builder{pb.prev_preds,pb.updater_function_raw,pb.curr_pred_raw};
		}

		
		template<typename Row, typename hd, typename... tl>
		auto E(const PredicateBuilder<Row, TypeList<hd,tl...> >& pb) {

			auto curr_pred_raw = pb.curr_pred_raw;
			//using the raw type here is necessary for renaming to be supported.
			//however, we can no longer rely on subtyping to enforce that the
			//correct Row_Extension type is passed in recursively.
			//this is why there is a wrapping in std::function within this function body.
			auto updater_f = [curr_pred_raw]
				(volatile auto& my_row, auto lookup_row, const int num_rows){
				using Prev_Row_Extension = typename std::decay_t<decltype(my_row)>::super;
				const std::function<bool (const volatile Row&, const volatile Prev_Row_Extension&)> curr_pred = curr_pred_raw;
				bool result = true;
				for (int i = 0; i < num_rows; ++i){
					auto rowpair = lookup_row(i);
					const volatile Prev_Row_Extension &your_row = rowpair.r;
					if (!curr_pred(rowpair.l, your_row)) result = false;
				}
				my_row.stored = result;
			};

			auto getter_f = [](volatile const Row&, volatile const auto &r){
				return r.stored;
			};
			
			using next_builder = PredicateBuilder<
				Row,
				TypeList<NamelessPredicateMetadata<bool,
												   predicate_builder::choose_uniqueness_tag<hd>::value,
												   decltype(updater_f),
												   decltype(getter_f) >,
						 hd,tl... >
				>;
			
			return next_builder{pb,updater_f,getter_f};
		}

		template<typename Row, typename hd, typename... tl>
		auto Min(const PredicateBuilder<Row, TypeList<hd,tl...> >& pb) {
			using min_t = typename hd::ExtensionType;
			
			auto curr_pred_raw = pb.curr_pred_raw;

			auto updater_f = [curr_pred_raw](volatile auto& my_row, auto lookup_row, const int num_rows){
				using Prev_Row_Extension = typename std::decay_t<decltype(my_row)>::super;
				const std::function<min_t (const volatile Row&, const volatile Prev_Row_Extension&)> curr_pred = curr_pred_raw;
				auto initial_val = lookup_row(0);
				min_t min = curr_pred(initial_val.l,initial_val.r);
				for (int i = 1; i < num_rows; ++i){
					auto rowpair = lookup_row(i);
					auto candidate = curr_pred(rowpair.l, rowpair.r);
					if (candidate < min) min = candidate;
				}
				my_row.stored = min;
			};
					
			auto getter_f = [](volatile const Row&, volatile const auto &r){
				return r.stored;
			};
			
			using next_builder =
				PredicateBuilder<
					Row,
				TypeList<
					NamelessPredicateMetadata<min_t,
											  predicate_builder::choose_uniqueness_tag<hd>::value,
											  decltype(updater_f),
											  decltype(getter_f)
											  >,hd,tl... > >;
			
			return next_builder{pb,updater_f,getter_f};
		}
	}

	//Evolving Predicates (combinator-free)

	template<typename NameEnum, NameEnum Name, typename F>
	struct Evolving_function {
		//types 
		using Fun = std::decay_t<decltype(convert(f))>;
		using SST = std::decay_t<typename function_traits<Fun>::template arg<0>::type>;
		static_assert(function_traits<Fun>::arity == 1,"Error: single-argument predicate only!");
		using Evolve_t = std::function<Fun (const SST&, int)>;

		//members
		const Fun f;
		const Evolve_t evolve;		
	};
	
	template<typename F>
	using Evolve_t = typename Evolving_function::Evolve_t;
	
	/**
	 * Required types:
	 * F : const SST& -> T
	 * Evolve : const SST&, int -> F
	 * where T is some arbitrary type
	 */
	template<typename NameEnum, NameEnum Name, typename F, typename Evolve>
	Evolving_function<NameEnum, Name, F> evolve(const F& f, const Evolve_t<F> &evolve){
		return Evolving_function<NameEnum, Name,F>{f,evolve};
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
