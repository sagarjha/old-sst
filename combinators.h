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

	using util::TypeList;

/**
 * Combinators for SST predicates.  These combinators can be used to define
 * new predicates using a simple logic language consisting of conjuction,
 * disjunction, integral-type comparison, and knowledge operators.
 */
	
	template<typename Row,typename ExtensionList>
	struct PredicateBuilder;

	template<typename Row,typename Entry>
	struct PredicateBuilder<Row,TypeList<Entry> > {

		template<typename T>
		using append = typename TypeList<Entry>::template append<T>;
		
		static_assert(std::is_pod<Row>::value,"Error: POD rows required!");
		struct Row_Extension{};
		const std::function<Entry (volatile const Row&, volatile const Row_Extension&, int)> curr_pred;
		using num_updater_functions = typename std::integral_constant<std::size_t, 0>::type;

		template<typename T>
		using Getters = std::conditional_t<
			Entry::has_name::value,
			std::tuple<std::function<Entry (T)> >,
			std::tuple<> >;
	};

	template<typename NameEnum, NameEnum Name, typename Ext_t>
	struct PredicateMetadata {
		using has_name = std::true_type;
		using Name_Enum = NameEnum;
		using name = std::integral_constant<NameEnum,Name>;
		using ExtensionType = Ext_t;
	};

	template<typename Ext_t, int unique_tag>
	struct NamelessPredicateMetadata {
		using has_name = std::false_type;
		using ExtensionType = Ext_t;
	};

	template<typename>
	struct choose_uniqueness_tag_str{
		using type = std::integral_constant<int,-1>;
	};
	
	template<typename NameEnum, NameEnum Name, typename Ext_t>
	struct choose_uniqueness_tag_str<PredicateMetadata<NameEnum, Name,Ext_t> >{
		using type = std::integral_constant<int, static_cast<int>(Name)>;
	};

	template<int uid, typename Ext_t>
	struct choose_uniqueness_tag_str<NamelessPredicateMetadata<Ext_t,uid> >{
		using type = std::integral_constant<int, uid>;
	};
	
	template<typename T>
	using choose_uniqueness_tag = typename choose_uniqueness_tag_str<T>::type;

	template<typename, typename...> struct NamedFunctionTuples;
	template<typename ...> struct NamedRowPredicates;
	//this is an immutable struct
	template<typename Row, typename ExtensionList>
	struct PredicateBuilder {

		template<typename T>
		using append = typename ExtensionList::template append<T>;

		using hd = typename ExtensionList::hd;
		using row_entry = typename hd::ExtensionType;
		using tl = typename ExtensionList::tl;
		
		struct Row_Extension : public PredicateBuilder<Row,tl>::Row_Extension {
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
			
		const updater_function_t updater_function;

		const PredicateBuilder<Row,tl> prev_preds;
		const std::function<row_entry (volatile const Row&, volatile const Row_Extension&)> curr_pred;

		PredicateBuilder(const PredicateBuilder<Row,tl> &prev, const updater_function_t &f, const decltype(curr_pred) curr_pred):
			updater_function(f),prev_preds(prev),curr_pred(curr_pred){}

		//for convenience when parameterizing SST
		using NamedRowPredicatesTypePack = NamedRowPredicates<PredicateBuilder>;
		using NamedFunctionTypePack = NamedFunctionTuples<void>;
	};

	namespace predicate_builder {

		template<int name_index, typename Row, typename NameEnum, NameEnum Name, typename Ext_t>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t> > > &pb){
			static_assert(static_cast<int>(Name) == name_index,"Error: names must be consequitive integer-valued enum members");
			return std::make_tuple(pb.curr_pred);
		}

		template<int name_index, typename Row, int uniqueness_tag, typename Ext_t>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag> > > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			return std::make_tuple(pb.curr_pred);
		}
		
		template<int name_index, typename Row, typename NameEnum, NameEnum Name, typename Ext_t, typename... tl>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t>,tl...> > &pb){
			static_assert(static_cast<int>(Name) == name_index,"Error: names must be consequitive integer-valued enum members");
			return std::tuple_cat(std::make_tuple(pb.curr_pred), extract_predicate_getters<name_index + 1>(pb.prev_preds));
		}

		template<int name_index, typename Row, int uniqueness_tag, typename Ext_t, typename... tl>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag>,tl...> > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			return std::tuple_cat(std::make_tuple(pb.curr_pred), extract_predicate_getters<name_index>(pb.prev_preds));
		}

		template<typename Result, typename F, typename Row, typename NameEnum, NameEnum Name, typename Ext_t>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t> > > &pb){
			using Row_Extension = typename std::decay_t(decltype(pb))::Row_Extension;
			Row_Extension *nptr{nullptr};
			accum.push_back(f(pb.updater_function,nptr));
		}

		template<typename Result, typename F, typename Row, int uniqueness_tag, typename Ext_t>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag> > > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			using Row_Extension = typename std::decay_t(decltype(pb))::Row_Extension;
			Row_Extension *nptr{nullptr};
			accum.push_back(f(pb.updater_function,nptr));
		}
		
		template<typename Result, typename F, typename Row, typename NameEnum, NameEnum Name, typename Ext_t, typename... tl>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t>,tl...> > &pb){
			using Row_Extension = typename std::decay_t(decltype(pb))::Row_Extension;
			Row_Extension *nptr{nullptr};
			accum.push_back(f(pb.updater_function,nptr));
			map_updaters(accum,f,pb.prev_preds);
		}

		template<typename Result, typename F, typename Row, int uniqueness_tag, typename Ext_t, typename... tl>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag>,tl...> > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			using Row_Extension = typename std::decay_t(decltype(pb))::Row_Extension;
			Row_Extension *nptr{nullptr};
			accum.push_back(f(pb.updater_function,nptr));
			map_updaters(accum,f,pb.prev_preds);
		}

		//F is a function of Row -> bool.  This will deduce that.
		template<typename F>
		auto as_row_pred(const F &f){
			using namespace util;
			using F2 = std::decay_t<decltype(convert(f))>;
			using undecayed_row = typename function_traits<F2>::template arg<0>::type;
			using Row = std::decay_t<undecayed_row>;
			using Entry = std::result_of_t<F(undecayed_row)>;
			using pred_builder = PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Entry,-1> > >;
			using Row_Extension = typename pred_builder::Row_Extension;
			return pred_builder{
				[f](const volatile Row &r, const volatile Row_Extension&){
					return f(r);},std::tuple<>{}};
		}

		template<int unique, typename Row, typename Ext>
		auto change_uniqueness(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,-1> > &pb){
			using next_builder = PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext,unique> > >;
			return next_builder{pb.curr_pred};
		}

		template<int unique, typename Row, typename Ext, typename... tl>
		auto change_uniqueness(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,-1>,tl...> &pb){
			using new_prev = change_uniqueness<unique>(pb.prev_preds);
			using This_list = typename decltype(new_prev)::template append <NamelessPredicateMetadata<Ext,unique> >;
			using next_builder = PredicateBuilder<Row, This_list>;
			return next_builder{new_prev,pb.updater_function,pb.curr_pred};
		}

		//nobody in this tree has a name yet;
		//we need to propogate a uniqueness-tag change
		//down the entire tree!
		template<typename NameEnum, NameEnum Name, typename Row, typename Ext, typename... tl>
		auto name_predicate(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,-1>,tl... > > &pb){
			constexpr int unique = static_cast<int>(Name);
			using new_prev = change_uniqueness<unique>(pb.prev_preds);
			using This_list = typename decltype(new_prev)::template append <PredicateMetadata<NameEnum,Name,Ext> >;
			using next_builder = PredicateBuilder<Row, This_list>;
			return next_builder{new_prev,pb.updater_function,pb.curr_pred};
		}

		//something else in here has been named, so we can just name this PB and need not touch previous ones
		template<typename NameEnum, NameEnum Name, typename Row, typename Ext, int unique, typename... tl>
		auto name_predicate(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,unique>,tl... > > &pb){
			static_assert(unique >= 0, "Internal error: overload resolution fails");
			using next_builder = PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum,Name,Ext>,tl...> >;
			return next_builder{pb.prev_preds,pb.updater_function,pb.curr_pred};
		}

		
		template<typename Row, typename hd, typename... tl>
		auto E(const PredicateBuilder<Row, TypeList<hd,tl...> >& pb) {
			
			using next_builder = PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<bool, choose_uniqueness_tag<hd> >,hd,tl... > >;
			using Row_Extension = typename next_builder::Row_Extension;

			auto curr_pred = pb.curr_pred;
			
			return next_builder{
				pb,
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

		template<typename Row, typename hd, typename... tl>
		auto Min(const PredicateBuilder<Row, TypeList<hd,tl...>, NameEnum, Name>& pb) {
			using min_t = hd::ExtensionType;
			using next_builder = PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<min_t, choose_uniqueness_tag<hd> >,hd,tl... > >;
			using Row_Extension = typename next_builder::Row_Extension;

			auto curr_pred = pb.curr_pred;
			
			return next_builder{
				pb,
					[curr_pred]
					(volatile Row_Extension& my_row, std::function<util::ref_pair<volatile Row,volatile Row_Extension> (int)> lookup_row, const int num_rows){
					auto initial_val = lookup_row(0);
					min_t min = curr_pred(initial_val.l,initial_val.r);
					for (int i = 1; i < num_rows; ++i){
						auto rowpair = lookup_row(i);
						auto candidate = curr_pred(rowpair.l, rowpair.r);
						if (candidate < min) min = candidate;
					}
					my_row.stored = min;
				},
					[](volatile const Row&, volatile const Row_Extension &r){
						return r.stored;
					}
			};
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
