#pragma once
#include <array>
#include <functional>
#include <memory>
#include <type_traits>
#include <cassert>
#include <tuple>
#include "args-finder.hpp"

namespace sst {
	namespace util {
		template<typename L, typename R>
		struct ref_pair{
			const L &l;
			const R &r;
		};
		
		
		template<typename T>
		constexpr T* mke_p(){
			return nullptr;
		}

		template<typename T, typename... pack> struct is_in_pack;

		template<typename T> struct is_in_pack<T> : std::false_type {};
		
		template<typename T, typename hd, typename... rst> struct is_in_pack<T,hd,rst...>
			: std::integral_constant<bool, std::is_same<T,hd>::value || is_in_pack<T,rst...>::value >::type {};

		template<typename...> struct dedup_params_str;
		
		template<>
		struct dedup_params_str<>{
			using types = std::tuple<>;
		};

		template<typename T, typename... rst>
		struct dedup_params_str<T, rst...>{
			using types = std::conditional_t<
				is_in_pack<T,rst...>::value,
				typename dedup_params_str<rst...>::types,
				std::decay_t<decltype(std::tuple_cat(std::declval<std::tuple<T> >(), std::declval<typename dedup_params_str<rst...>::types>()))>
				>;
		};

		template<typename... T>
		using dedup_params = typename dedup_params_str<T...>::types;

		template<typename... tuple_contents >
		auto* extend_tuple_members_f (const std::tuple<tuple_contents...>&){
			struct extend_this : public tuple_contents... {};
			return util::mke_p<extend_this>();
		}
		
		template<typename tpl>
		using extend_tuple_members = std::decay_t<decltype(*extend_tuple_members_f(std::declval<tpl>()))>;

		template<typename... T>
		using extend_all = extend_tuple_members<dedup_params<T...> >;

		template<template <typename> class Pred, typename typelist>
		constexpr std::enable_if_t<typelist::is_tail::value,bool> forall_type_list(){
			using hd = typename typelist::hd;
			return Pred<hd>::value;
		}
		
		template<template <typename> class Pred, typename typelist>
		constexpr std::enable_if_t<!typelist::is_tail::value,bool> forall_type_list(){
			using hd = typename typelist::hd;
			using rst = typename typelist::rst;
			return Pred<hd>::value && forall_type_list<Pred, rst>();
		}

		template<typename T1>
		constexpr auto sum(const T1 &t1){
			return t1;
		}
		
		template<typename T1, typename T2, typename... T>
		constexpr auto sum(const T1 &t1, const T2 &t2, const T& ... t){
			return t1 + sum(t2,t...);
		}

		template<std::size_t n, typename T>
		struct n_copies_str;
		
		template<typename T>
		struct n_copies_str<0,T> {
			using type = std::tuple<>;
		};

		template<std::size_t n, typename T>
		struct n_copies_str{
			using type = std::decay_t<decltype(
				std::tuple_cat(std::make_tuple(std::declval<T>()),
							   std::declval<typename n_copies_str<n-1,T>::type>() ))>;
		};

		template<std::size_t n, typename T>
		using n_copies = typename n_copies_str<n,T>::type;
	}
}
