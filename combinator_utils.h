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

	}
}
