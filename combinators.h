#pragma once
#include "sst.h"
#include "predicates.h"
#include <array>

namespace sst {

using std::function;
using std::list;
using std::pair;


/**
 * Combinators for SST predicates.  These combinators can be used to define
 * new predicates using a simple logic language consisting of conjuction,
 * disjunction, integral-type comparison, and knowledge operators.
 */
	class Combinators {
	public:
		
		using table_t = std::pair<std::unique_ptr<volatile InternalRow[]>,const int num_rows>;
		
		struct RowPred{
			const std::function<bool, (const InternalRow&)> f;
			operator std::function<bool, (const InternalRow&)> (){
				return f;
			}
			RowPred(std::function<bool, (const InternalRow&)> f):f(f){}
			RowPred(std::function<bool, (const Row&)> f):
				f([f](const InternalRow& r){return f(r);}){}
		};
		
		struct SSTPred{
			const std::function<bool, (const table_t&)> f;
			operator std::function<bool, (const table_t&)>(){return f;}
			SSTPred(std::function<bool, (const table_t&)> f):f(f){}
			SSTPred(RowPred rp):
				f([f = rp.f](const table_t& sst){
						//TODO: this is slow.  Need const-access to each row.
						auto owner = sst.get_snapshot();
						return f(owner->get(ss.get_local_index()));
					})
				{}
		};
		
		template<typename GeneratedFunctionTuples, typename LastPred, typename... Predicates>
		struct PredSet{
			static_assert(std::is_same<LastPred,RowPred>::value || std::is_same<LastPred,SSTPred>::value,
						  "Error: PredSet is a set of predicates");

			//if LastPred is an SST pred, building a predicate atop it would require
			//making a name. 
			
			const LastPred f;
			PredSet(RowPred rp);
			PredSet(SSTPred sstp);
			
			PredSet(std::function<bool, (const InternalRow&)> f):
				PredSet(RowPred(f)){}
			PredSet(std::function<bool, (const Row&)> f):
				PredSet(RowPred(f)){}
			PredSet(std::function<bool, (const table_t&)> f):
				PredSet(SSTPred(f)){}
		};
		
		template<typename GeneratedFunctionTuples, typename LastPred, typename... RestPreds>
		auto E(PredSet<GeneratedFunctionTuples, LastPred, RestPreds...> rp){
			return SSTPred{[rp](const table_t& sst){
					for (int i = 0; i < sst.second; ++i){
						if (!rp.f(sst.first[i])) return false;
					}
					return true;
				}};
		}

	};

	
	template<typename Row>
	struct Predicate {
		
		struct Row_Extension : public Row{
			//this is an array
			bool stored*;
			const std::size_t sizeof_stored;
			//BUYER BEWARE:  SST will need to delete the array here.
			//Row_Extension must remain trivially copyable.
		};
		
		using updater_function_t =
			std::function<void (Row_Extension&, std::function<const Row_Extension& (int)>, const int num_rows)>;
								
								//this is an immutable struct
		template<std::size_t num_stored_bools>
		struct PredicateBuilder {

			using updater_function_array_t = const std::array<updater_function_t,num_stored_bools>;
			
			updater_function_array_t updater_functions;
			
			const std::function<bool (const Row_Extension& r)> curr_pred;
			
			using next_builder = PredicateBuilder<num_stored_bools + 1>;
			using next_function_array_t = typename next_builder::updater_function_array_t;
			
			next_builder E() const {
				
				next_function_array_t next_funs;
				
				for (int i = 0; i < num_stored_bools; ++i){
					next_funs[i] = updater_functions[i];
				}
				
				next_funs[num_stored_bools] = [curr_pred](Row_Extension& my_row, std::function<const Row_Extension& (int)> lookup_row, const int num_rows){
					bool result = true;
					for (int i = 0; i < num_rows; ++i){
						if (!curr_pred(lookup_row(i))) result = false;
					}
					assert(my_row.sizeof_stored == num_stored_bools + 1);
					my_row.stored[num_stored_bools] = result;
				};

				return next_builder{
					next_funs,
						[](const Row_Extension &r){
						return r.stored[num_stored_bools];
					}
					}}
		};

								};
						  
	namespace predicate_builder {
		template<typename Row>
		typename Predicate<Row>:: template PredicateBuilder<1> E(std::function<bool (const Row& r)> f){
			return typename Predicate<Row>:: template PredicateBuilder<1>{{{}},[f](const Row_Extension &r){return f(r)}}.E();
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
