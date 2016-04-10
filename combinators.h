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

	template<typename NameEnum, NameEnum Name, typename Ext_t, typename RawUpdater, typename RawGetter>
	struct PredicateMetadata {
		using has_name = std::true_type;
		using Name_Enum = NameEnum;
		using name = std::integral_constant<NameEnum,Name>;
		using ExtensionType = Ext_t;
		using raw_updater = RawUpdater;
		using raw_getter = RawGetter;
	};

	template<typename Ext_t, int unique_tag, typename RawUpdater, typename RawGetter>
	struct NamelessPredicateMetadata {
		using has_name = std::false_type;
		using ExtensionType = Ext_t;
		using raw_updater = RawUpdater;
		using raw_getter = RawGetter;
	};

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

	template<typename,typename> struct NameEnumMatches_str;

	template<typename NameEnum, typename NameEnum2, NameEnum2 Name, typename... Ext_t>
	struct NameEnumMatches_str<NameEnum, TypeList<PredicateMetadata<NameEnum2, Name, Ext_t...> > >
		: std::integral_constant<bool,
								 std::is_same<NameEnum, NameEnum2>::value>{};
	
	template<typename NameEnum, int unique, typename Ext_t, typename... rst>
	struct NameEnumMatches_str<NameEnum, TypeList<NamelessPredicateMetadata<Ext_t,unique,rst...> > >
		: std::true_type {
		static_assert(unique >= 0, "Error: this query should be performed only on named RowPredicates. This does not have a name yet.");
	};
	
	template<typename NameEnum, typename NameEnum2, NameEnum2 Name, typename Ext_t, typename Up, typename Get, typename hd, typename... tl>
	struct NameEnumMatches_str<NameEnum, TypeList<PredicateMetadata<NameEnum2, Name, Ext_t,Up,Get>,hd,tl... > >
		: std::integral_constant<bool,
								 NameEnumMatches_str<NameEnum,TypeList<hd,tl...> >::value && 
								 std::is_same<NameEnum, NameEnum2>::value>{};
	
	template<typename NameEnum, int unique, typename Ext_t, typename Up, typename Get, typename hd, typename... tl>
	struct NameEnumMatches_str<NameEnum, TypeList<NamelessPredicateMetadata<Ext_t,unique,Up,Get>,hd,tl... > >
		: std::integral_constant<bool,
								 NameEnumMatches_str<NameEnum,TypeList<hd,tl...> >::value>{};

	template<typename NameEnum, typename List>
	using NameEnumMatches = typename NameEnumMatches_str<NameEnum,List>::type;

	template<typename>
	struct choose_uniqueness_tag_str{
		using type = std::integral_constant<int,-1>;
	};
	
	template<typename NameEnum, NameEnum Name, typename... Ext_t>
	struct choose_uniqueness_tag_str<PredicateMetadata<NameEnum, Name,Ext_t...> >{
		using type = std::integral_constant<int, static_cast<int>(Name)>;
	};

	template<int uid, typename Ext_t, typename... rst>
	struct choose_uniqueness_tag_str<NamelessPredicateMetadata<Ext_t,uid,rst...> >{
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
		using is_named = std::integral_constant<bool, (choose_uniqueness_tag<hd>::value >= 0)>;

		static_assert(is_named::value ? choose_uniqueness_tag<hd>::value >= 0 : true, "Internal Error: renaming failed!");
		
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
		using name_enum_matches = NameEnumMatches<NameEnum, ExtensionList>;
			
		const typename hd::raw_updater updater_function_raw;
		const updater_function_t updater_function;

		const PredicateBuilder<Row,tl> prev_preds;
		using pred_t = std::function<row_entry (volatile const Row&, volatile const Row_Extension&)>;
		const typename hd::raw_getter curr_pred_raw;
		const pred_t curr_pred;

/*
		auto curr_pred = pb.curr_pred;
		using Getter_t = std::decay_t<std::result_of_t<decltype(curr_pred)(volatile const InternalRow&, volatile const InternalRow&, int)> >;
		std::function<Getter_t (volatile const InternalRow&, int pre_num)> getter = [curr_pred](volatile const InternalRow& row, int pre_num){
			return curr_pred(row,row,pre_num);
		};
 */
		
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

		template<int name_index, typename Row, typename NameEnum, NameEnum Name, typename... Ext_t>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t...> > > &pb){
			static_assert(static_cast<int>(Name) == name_index,"Error: names must be consequitive integer-valued enum members");
			return std::make_tuple(pb.curr_pred);
		}

		template<int name_index, typename Row, int uniqueness_tag, typename Ext_t, typename... rst>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag,rst...> > > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			return std::make_tuple(pb.curr_pred);
		}
		
		template<int name_index, typename Row, typename NameEnum, NameEnum Name, typename Ext_t, typename Up, typename Get, typename... tl>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t,Up,Get>,tl...> > &pb){
			static_assert(static_cast<int>(Name) == name_index,"Error: names must be consequitive integer-valued enum members");
			return std::tuple_cat(std::make_tuple(pb.curr_pred), extract_predicate_getters<name_index + 1>(pb.prev_preds));
		}

		template<int name_index, typename Row, int uniqueness_tag, typename Ext_t, typename Up, typename Get, typename... tl>
		auto extract_predicate_getters(const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag,Up,Get>,tl...> > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			return std::tuple_cat(std::make_tuple(pb.curr_pred), extract_predicate_getters<name_index>(pb.prev_preds));
		}

		template<typename Result, typename F, typename Row, typename NameEnum, NameEnum Name, typename... Ext_t>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t...> > > &pb){
			//This should be the tail. Nothing here.
		}

		template<typename Result, typename F, typename Row, int uniqueness_tag, typename Ext_t, typename... rst>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag,rst...> > > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			//This should be the tail. Nothing here.
		}

		template<typename Result, typename F, typename Row, int uniqueness_tag, typename Ext_t, typename Up, typename Get, typename hd, typename... tl>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext_t, uniqueness_tag,Up,Get>,hd,tl...> > &pb){
			static_assert(uniqueness_tag >= 0,"Error: Please name this predicate before attempting to use it");
			using Row_Extension = typename std::decay_t<decltype(pb)>::Row_Extension;
			Row_Extension *nptr{nullptr};
			accum.push_back(f(pb.updater_function,nptr));
			map_updaters(accum,f,pb.prev_preds);
		}
		
		template<typename Result, typename F, typename Row, typename NameEnum, NameEnum Name, typename Ext_t, typename Up, typename Get, typename hd, typename... tl>
		auto map_updaters(std::vector<Result> &accum, const F &f, const PredicateBuilder<Row, TypeList<PredicateMetadata<NameEnum, Name, Ext_t,Up,Get>,hd,tl...> > &pb){
			using Row_Extension = typename std::decay_t<decltype(pb)>::Row_Extension;
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
			auto pred_f = [f](const volatile Row &r, const volatile auto&){
				return f(r);};
			using pred_builder = PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Entry,-1,void, decltype(pred_f)> > >;
			return pred_builder{pred_f,pred_f};
		}

		template<int unique, typename Row, typename Ext, typename Get>
		auto change_uniqueness(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,-1,void,Get> > > &pb){
			using next_builder = PredicateBuilder<Row, TypeList<NamelessPredicateMetadata<Ext,unique,void,Get> > >;
			return next_builder{pb.curr_pred_raw};
		}

		template<int unique, typename Row, typename Ext, typename Up, typename Get, typename hd, typename... tl>
		auto change_uniqueness(const PredicateBuilder<Row,TypeList<NamelessPredicateMetadata<Ext,-1,Up,Get>,hd,tl...> > &pb){
			auto new_prev = change_uniqueness<unique>(pb.prev_preds);
			using This_list = typename decltype(new_prev)::template append <NamelessPredicateMetadata<Ext,unique,Up,Get> >;
			using next_builder = PredicateBuilder<Row, This_list>;
			return next_builder{new_prev,pb.updater_function_raw,pb.curr_pred_raw};
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
			//this is why there is a Prev_Row_Extension temporary defined
			//explicitly.  More testing is warranted to ensure that this is
			//sufficient. 
			auto updater_f = [curr_pred_raw]
				(volatile auto& my_row, auto lookup_row, const int num_rows){
				using Prev_Row_Extension = typename std::decay_t<decltype(my_row)>::super;
				bool result = true;
				for (int i = 0; i < num_rows; ++i){
					auto rowpair = lookup_row(i);
					const volatile Prev_Row_Extension &your_row = rowpair.r;
					if (!curr_pred_raw(rowpair.l, your_row)) result = false;
				}
				my_row.stored = result;
			};

			auto getter_f = [](volatile const Row&, volatile const auto &r){
				return r.stored;
			};
			
			using next_builder = PredicateBuilder<Row,
												  TypeList<NamelessPredicateMetadata<bool,
																					 choose_uniqueness_tag<hd>::value,
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
				auto initial_val = lookup_row(0);
				using Prev_Row_Extension = typename std::decay_t<decltype(my_row)>::super;
				const volatile Prev_Row_Extension &your_row = initial_val.r;
				min_t min = curr_pred_raw(initial_val.l,your_row);
				for (int i = 1; i < num_rows; ++i){
					auto rowpair = lookup_row(i);
					const volatile Prev_Row_Extension &your_row = rowpair.r;
					auto candidate = curr_pred_raw(rowpair.l, your_row);
					if (candidate < min) min = candidate;
				}
				my_row.stored = min;
			};
					
			auto getter_f = [](volatile const Row&, volatile const auto &r){
				return r.stored;
			};
			
			using next_builder =
				PredicateBuilder<Row,
								 TypeList<
									 NamelessPredicateMetadata<min_t,
															   choose_uniqueness_tag<hd>::value,
															   decltype(updater_f),
															   decltype(getter_f)
															   >,hd,tl... > >;

			
			
			return next_builder{pb,updater_f,getter_f};
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
