#ifndef SST_H
#define SST_H

#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "util.h"
#include "named_function.h"
#include "verbs.h"
#include "NamedRowPredicates.h"
#include "combinators.h"
#include "combinator_utils.h"

/** The root namespace for the Shared State Table project. */
namespace sst {

using std::vector;
using std::cout;
using std::endl;
using std::function;
using std::list;
using std::pair;
using std::map;
using std::thread;
using std::unique_ptr;

using util::template_and;

/**
 * Flag to determine whether an SST is implemented in reads mode or writes mode.
 */
enum class Mode {
    /** In reads mode, the SST will continuously refresh the local copy of
     * the table by posting one-sided reads to the remote nodes. */
    Reads,
    /** In writes mode, the SST waits for its local copy of the table to be
     * updated by a one-sided write from a remote node, and each node is
     * responsible for calling SST::put() to initiate this write after
     * changing a variable in its local row. */
    Writes
};



template<typename NamedFunctionParam, typename ... NamedFunctionRets>
struct NamedFunctionTuples {
        static_assert(template_and(std::is_pod<NamedFunctionRets>::value...), "Error! Named functions must return POD.");
        using return_types = std::tuple<NamedFunctionRets...>;
        using function_types = std::tuple<NamedFunctionRets (*) (NamedFunctionParam&) ...>;
};

	
/**
 * The SST object, representing a single shared state table.
 *
 * @tparam Row The type of the structure that will be used for each row in
 * this SST
 * @tparam ImplMode A {@link Mode} enum value indicating whether this SST will be
 * implemented in Reads mode or Writes mode; default is Writes.
 * @tparam NameEnum An enum that will be used to name functions, if named
 * functions are used with this SST. Defaults to util::NullEnum, which is empty.
 * @tparam NamedFunctionTypePack A struct containing the function types and
 * return types of all the named functions that will be registered with this
 * SST; defaults to an empty struct if no named functions are used.
 */
template<class Row, Mode ImplMode = Mode::Writes,
		 typename NameEnum = util::NullEnum,
		 typename NamedFunctionTypePack = NamedFunctionTuples<void>,
		 typename NamedRowPredicatesTypePack = NamedRowPredicates<> >
class SST {
        //Row struct must be POD. In addition, it should not contain any pointer types
	static_assert(std::is_pod<Row>::value, "Error! Row type must be POD.");
	//static_assert(forall_type_list<this should be is_base with the Row!, NamedRowPredicatesTypePack>(),"Error: RowPredicates built on wrong row!");
	
	
    private:
	struct InternalRow : public Row, public util::extend_tuple_members<typename NamedRowPredicatesTypePack::row_types> {
		typename NamedFunctionTypePack::return_types observed_values;
	};

	using named_functions_t =
		std::decay_t<decltype(
		std::tuple_cat(std::declval<typename NamedFunctionTypePack::function_types>(),
					   std::declval<util::n_copies<NamedRowPredicatesTypePack::size::value,
					                               std::function<bool (const SST&)> > >()))>;
	/** List of functions we have registered to use knowledge operators on */
	named_functions_t named_functions;

    public:
        /**
         * An object containing a read-only snapshot of an SST. It can be used
         * like an SST for reading row values and the states of named functions,
         * but it is disconnected from the actual SST and will not get updated.
         */
        class SST_Snapshot {
            private:
                /** Number of members, which is the number of rows in `table`. */
                int num_members;
                /** The structure containing shared state data. */
                unique_ptr<const InternalRow[]> table;
                /** List of functions we have registered to use knowledge operators on */
                typename NamedFunctionTypePack::function_types named_functions;

			//Note to self: there is no requirement to include row function mechanisms here.

            public:
                /** Creates an SST snapshot given the current state internals of the SST. */
                SST_Snapshot(const unique_ptr<volatile InternalRow[]>& _table, int _num_members,
							 const decltype(named_functions)& _named_functions);
                /** Copy constructor. */
                SST_Snapshot(const SST_Snapshot& to_copy);

                /** Accesses a row of the snapshot. */
                const Row & get(int index) const;
                /** Accesses a row of the snapshot using the [] operator. */
                const Row & operator[](int index) const;
        };

    private:
        /** List of members in the group (values are node ranks). */
        vector<int> members;
        /** List of node ranks mapped to member index. */
        map<int, int, std::greater<int>> members_by_rank;
        /** Number of members; equal to `members.size()`. */
        int num_members;
        /** Index of this node in the table. */
        int member_index;
        /** The actual structure containing shared state data. */
        unique_ptr<volatile InternalRow[]> table;
	/** List of functions needed to update row predicates */
	using row_predicate_updater_p = std::unique_ptr<std::function<void (SST&)> >;
	std::vector<row_predicate_updater_p> row_predicate_updater_functions; //should be of size NamedPredicatesTypePack::num_updater_functions:::value
	
        /** RDMA resources vector, one for each member. */
        vector<unique_ptr<resources>> res_vec;
        /** A flag to signal background threads to shut down; set to true during destructor calls. */
        bool thread_shutdown;

        /** Base case for the recursive constructor_helper with no template parameters. */
        template<int index>
        auto constructor_helper() {
            return std::tuple<>{};
        }
        /** Helper function for the constructor that recursively unpacks NamedFunction template parameters. */
        template<int index, NameEnum Name, typename NamedFunctionRet, typename... RestFunctions>
        auto constructor_helper(const NamedFunction<NameEnum, Name, const SST, NamedFunctionRet> &firstFunc, const RestFunctions&... rest) {
            using namespace std;
            static_assert(static_cast<int>(Name) == index, "Error: non-enum name, or name used out-of-order.");
            return tuple_cat(make_tuple(firstFunc.fun), constructor_helper<index + 1>(rest...));
        }

	/** Helper function for the constructor that recursively unpacks Named RowPredicate template parameters. */

	template<int index, NameEnum Name, std::size_t num_stored_bools, typename... RestFunctions>
	auto constructor_helper(const PredicateBuilder<Row,num_stored_bools,NameEnum,Name> &pb, const RestFunctions&... rest){
		using namespace std;
		using Row_Extension = typename std::decay_t<decltype(pb)>::Row_Extension;
		static_assert(static_cast<int>(Name) == index, "Error: non-enum name, or name used out-of-order.");
		for_each([&](const auto& f){
				row_predicate_updater_functions.push_back(make_unique<std::function<void (SST&)> >([f](SST& sst) -> void{
							f(sst.table[sst.get_local_index()],
							  [&](int row){return util::ref_pair<volatile Row,volatile Row_Extension>{sst.table[row],sst.table[row]}; },
							  sst.get_num_rows());
						}));
			}, pb.updater_functions);
		auto curr_pred = pb.curr_pred;
		std::function<bool (const SST&)> getter = [curr_pred](const SST& sst){
			const auto &row = sst.table[sst.get_local_index()];
			return curr_pred(row,row);};
		return tuple_cat(make_tuple(getter), constructor_helper<index + 1>(rest...));
	}

        //Functions for background threads to run
        /** Reads all the remote rows once by RDMA, if this SST is in Reads mode. */
        void refresh_table();
        /** Continuously refreshes all the remote rows, if this SST is in Reads mode. */
        void read();
        /** Continuously evaluates predicates to detect when they become true. */
        void detect();


    public:
        /**
         * Constructs an SST instance, initializes RDMA resources, and spawns
         * background threads.
         *
         * @param _members A vector of node ranks (IDs), each of which represents a
         * node participating in the SST. The order of nodes in this vector is the
         * order in which their rows will appear in the SST.
         * @param _node_rank The node rank of the local node, i.e. the one on which
         * this code is running.
         * @param named_funs Zero or more NamedFunction structs representing named
         * functions whose values the SST should track
         */
	template<typename... NamedFunctions>
	SST(const vector<int> &_members, int _node_rank) :
		SST(_members, _node_rank,std::tuple<>{}) {}

	template<NameEnum Name, std::size_t num_stored_bools, typename... RestFunctions>
	SST(const vector<int> &_members, int _node_rank, const PredicateBuilder<Row,num_stored_bools,NameEnum,Name> &pb, RestFunctions... named_funs) :
		SST(_members, _node_rank,constructor_helper<0>(pb,named_funs...)) {}
	
	template<NameEnum Name, typename NamedFunctionRet, typename... RestFunctions>
	SST(const vector<int> &_members, int _node_rank, const NamedFunction<NameEnum, Name, const SST, NamedFunctionRet> &firstFunc, RestFunctions... named_funs) :
		SST(_members, _node_rank,constructor_helper<0>(firstFunc,named_funs...)) {}
        /**
         * Delegate constructor to construct an SST instance without named
         * functions.
         * @param _members A vector of node ranks (IDs), each of which represents a
         * node participating in the SST. The order of nodes in this vector is the
         * order in which their rows will appear in the SST.
         * @param _node_rank The node rank of the local node, i.e. the one on which
         * this code is running.
         */
	SST(const vector<int> &_members, int _node_rank, decltype(named_functions));
        virtual ~SST();
        /** Accesses a local or remote row. */
        volatile Row & get(int index);
        /** Read-only access to a local or remote row, for use in const contexts. */
        const volatile Row & get(int index) const;
        /** Accesses a local or remote row using the [] operator. */
        volatile Row & operator [](int index);
        /** Read-only [] operator for a local or remote row, for use in const contexts. */
        const volatile Row & operator[](int index) const;
        int get_num_rows() const;
        /** Gets the index of the local row in the table. */
        int get_local_index() const;
        /** Gets a snapshot of the table. */
        std::unique_ptr<SST_Snapshot> get_snapshot() const;
        /** Writes the local row to all remote nodes. */
        void put();
        /** Writes a contiguous subset of the local row to all remote nodes. */
        void put(long long int offset, long long int size);
        /** Does a TCP sync with each member of the SST. */
        void sync_with_members() const;

        class Predicates;
        /** Predicate management object for this SST. */
        Predicates& predicates;
};

} /* namespace sst */

#include "sst_impl.h"

#endif /* SST_H */
