#ifndef SST_H
#define SST_H

#include <cassert>
#include <cstring>
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

#include "predicates.h"
#include "tcp.h"
#include "verbs.h"

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

/**
 * The SST object, representing a single shared state table.
 *
 * @tparam Row The type of the structure that will be used for each row in
 * this SST
 * @tparam ImplMode A {@link Mode} enum value indicating whether this SST will be
 * implemented in Reads mode or Writes mode; default is Writes.
 */
template<class Row, Mode ImplMode = Mode::Writes>
class SST {
        //Row struct must be POD. In addition, it should not contain any pointer types
        static_assert(std::is_pod<Row>::value, "Error! Row type must be POD.");

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
        unique_ptr<volatile Row[]> table;
        /** RDMA resources vector, one for each member. */
        vector<unique_ptr<resources>> res_vec;
        /** A flag to signal background threads to shut down; set to true during destructor calls. */
        bool thread_shutdown;
        //Functions for background threads to run
        /** Reads all the remote rows once by RDMA, if this SST is in Reads mode. */
        void refresh_table();
        /** Continuously refreshes all the remote rows, if this SST is in Reads mode. */
        void read();
        /** Continuously evaluates predicates to detect when they become true. */
        void detect();

    public:
        SST(const vector<int> &_members, int _node_rank);
        virtual ~SST();
        /** Accesses a local or remote row. */
        volatile Row & get(int index);
        /** Accesses a local or remote row using the [] operator. */
        volatile Row & operator [](int index);
        int get_num_rows() const;
        /** Gets the the index of the local row in the table. */
        int get_local_index() const;
        /** Gets a snapshot of the table. */
        unique_ptr<const Row[]> get_snapshot() const;
        /** Writes the local row to all remote nodes. */
        void put();
        /** Writes a contiguous subset of the local row to all remote nodes. */
        void put(long long int offset, long long int size);
        /** Does a TCP sync with each member of the SST. */
        void sync_with_members() const;

        /** Predicate management object for this SST. */
        Predicates<Row, ImplMode> predicates;
};

/** 
 * Constructs an SST instance, initializes RDMA resources, and spawns
 * background threads.
 *
 * @param _members A vector of node ranks (IDs), each of which represents a
 * node participating in the SST. The order of nodes in this vector is the
 * order in which their rows will appear in the SST.
 * @param _node_rank The node rank of the local node, i.e. the one on which
 * this code is running.
 */
template<class Row, Mode ImplMode>
SST<Row, ImplMode>::SST(const vector<int> & _members, int _node_rank) :
        members(_members.size()), num_members(_members.size()),
        table(new Row[_members.size()]), res_vec(num_members),
        thread_shutdown(false) {

    // copy members and figure out the member_index 
    for (int i = 0; i < num_members; ++i) {
        members[i] = _members[i];
        if (members[i] == _node_rank) {
            member_index = i;
        }
    }

    //sort members descending by node rank, while keeping track of their specified index in the SST
    for (int sst_index = 0; sst_index < num_members; ++sst_index) {
        members_by_rank[members[sst_index]] = sst_index;
    }

    //Static dispatch of implementation code based on the template parameter
    if (ImplMode == Mode::Reads) {
        // initialize each element of res_vec
        int node_rank, sst_index;
        for (auto const& rank_index : members_by_rank) {
            std::tie(node_rank, sst_index) = rank_index;
            if (sst_index != member_index) {
                // exchange lkey and addr of the table via tcp for enabling rdma reads
                res_vec[sst_index] = std::make_unique<resources>(node_rank,
                        (char *) &(table[member_index]),
                        (char *) &(table[sst_index]), sizeof(table[0]),
                        sizeof(table[0]));
            }
        }

        // create the reader and the detector thread
        thread reader(&SST::read, this);
        thread detector(&SST::detect, this);
        reader.detach();
        detector.detach();

        cout << "Initialized SST and Started Threads" << endl;
    } else {
        // initialize each element of res_vec
        int node_rank, sst_index;
        for (auto const& rank_index : members_by_rank) {
            std::tie(node_rank, sst_index) = rank_index;
            if (sst_index != member_index) {
                // exchange lkey and addr of the table via tcp for enabling rdma writes
                res_vec[sst_index] = std::make_unique<resources>(node_rank,
                        (char *) &(table[sst_index]),
                        (char *) &(table[member_index]), sizeof(table[0]),
                        sizeof(table[0]));
            }
        }

        thread detector(&SST::detect, this);
        detector.detach();

        cout << "Initialized SST and Started Threads" << endl;
    }

}

/**
 * Destructor for the state table; sets thread_shutdown to true so that
 * detached background threads exit cleanly.
 */
template<class Row, Mode ImplMode>
SST<Row, ImplMode>::~SST() {
    thread_shutdown = true;
}

/** 
 * Although a mutable reference is returned, only the local row should be 
 * modified through this function. Modifications to remote rows will not be 
 * propagated to other nodes and may be overwritten at any time when the SST
 * system updates those remote rows.
 *
 * @param index The index of the row to access.
 * @return A reference to the row structure stored at the requested row.
 */
template<class Row, Mode ImplMode>
volatile Row & SST<Row, ImplMode>::get(int index) {
    // check that the index is within range
    assert(index >= 0 && index < num_members);

    // return the table entry
    return table[index];
}

/**
 * Simply calls the get function.
 */
template<class Row, Mode ImplMode>
volatile Row & SST<Row, ImplMode>::operator [](int index) {
    return get(index);
}

/**
 * @return The number of rows in the table.
 */
template<class Row, Mode ImplMode>
int SST<Row, ImplMode>::get_num_rows() const {
    return num_members;
}

/**
 * This is the index of the local node, i.e. the node on which this code is
 * running, with respect to the group. `sst_instance[sst_instance.get_local_index()]`
 * will always returna reference to the local node's row.
 *
 * @return The index of the local row.
 */
template<class Row, Mode ImplMode>
int SST<Row, ImplMode>::get_local_index() const {
    return member_index;
}

/**
 * This is a deep copy of the table that can be used for predicate evaluation,
 * which will no longer be affected by remote nodes updating their rows.
 *
 * @return A copy of all the SST's rows in their current state.
 */
template<class Row, Mode ImplMode>
unique_ptr<const Row[]> SST<Row, ImplMode>::get_snapshot() const {
    Row* copy = new Row[num_members];
    std::memcpy(copy, const_cast<const Row*>(table.get()), num_members * sizeof(Row));
    return unique_ptr<const Row[]>(copy);
}

/**
 * Exchanges a single byte of data with each member of the SST group over the
 * TCP (not RDMA) connection, in descending order of the members' node ranks.
 * This creates a synchronization barrier, since the TCP reads are blocking,
 * and should be called after SST initialization to ensure all nodes have
 * finished initializing their local SST code.
 */
template<class Row, Mode ImplMode>
void SST<Row, ImplMode>::sync_with_members() const {
    int node_rank, sst_index;
    for (auto const& rank_index : members_by_rank) {
        std::tie(node_rank, sst_index) = rank_index;
        if (sst_index != member_index) {
            tcp::sync(node_rank);
        }
    }
}


/**
 * If this SST is in Writes mode, this function does nothing.
 */
template<class Row, Mode ImplMode>
void SST<Row, ImplMode>::refresh_table() {
    if (ImplMode == Mode::Reads) {
        for (int index = 0; index < num_members; ++index) {
            if (index == member_index) {
                // don't read own row!
                continue;
            }
            // perform a remote RDMA read on the owner of the row
            res_vec[index]->post_remote_read(sizeof(table[0]));
        }
        // poll for one less than number of rows
        for (int index = 0; index < num_members - 1; ++index) {
            // poll for completion
            verbs_poll_completion();
        }
    }
}

/**
 * If this SST is in Reads mode, this function is run in a detached background
 * thread to continuously keep the local SST table updated. If this SST is in
 * Writes mode, this function does nothing.
 */
template<class Row, Mode ImplMode>
void SST<Row, ImplMode>::read() {
    if(ImplMode == Mode::Reads) {
        while (!thread_shutdown) {
            refresh_table();
        }
        cout << "Reader thread shutting down" << endl;
    }
}

/**
 * This function is run in a detached background thread to detect predicate
 * events. It continuously evaluates predicates one by one, and runs the
 * trigger functions for each predicate that fires.
 */
template<class Row, Mode ImplMode>
void SST<Row, ImplMode>::detect() {
    while (!thread_shutdown) {
        // one time predicates need to be evaluated only until they become true
        auto pred_it = predicates.one_time_predicates.begin();
        while (pred_it != predicates.one_time_predicates.end()) {
            if (pred_it->first(*this) == true) {
                for (auto func : pred_it->second) {
                    func(*this);
                }
                // erase the predicate as it was just found to be true
                pred_it = predicates.one_time_predicates.erase(pred_it);
            } else {
                pred_it++;
            }
        }

        // recurrent predicates are evaluated each time they are found to be true
        for (pred_it = predicates.recurrent_predicates.begin();
                pred_it != predicates.recurrent_predicates.end(); ++pred_it) {
            if (pred_it->first(*this) == true) {
                for (auto func : pred_it->second) {
                    func(*this);
                }
            }
        }

        // transition predicates are only evaluated when they change from false to true
        pred_it = predicates.transition_predicates.begin();
        auto pred_state_it = predicates.transition_predicate_states.begin();
        while (pred_it != predicates.transition_predicates.end()) {
            //*pred_state_it is the previous state of the predicate at *pred_it
            bool curr_pred_state = pred_it->first(*this);
            if (curr_pred_state == true && *pred_state_it == false) {
                for (auto func : pred_it->second) {
                    func(*this);
                }
            }
            *pred_state_it = curr_pred_state;

            ++pred_it;
            ++pred_state_it;
        }
    }
    cout << "Predicate detection thread shutting down" << endl;
}

/**
 * This writes the entire local row, using a one-sided RDMA write, to all of
 * the other members of the SST group. If this SST is in Reads mode, this
 * function does nothing.
 */
template<class Row, Mode ImplMode>
void SST<Row, ImplMode>::put() {
    if (ImplMode == Mode::Writes) {
        for (int index = 0; index < num_members; ++index) {
            if (index == member_index) {
                // don't write to yourself!
                continue;
            }
            // perform a remote RDMA write on the owner of the row
            res_vec[index]->post_remote_write(sizeof(table[0]));
        }
        // poll for one less than number of rows
        for (int index = 0; index < num_members - 1; ++index) {
            // poll for completion
            verbs_poll_completion();
        }
    }
}

/**
 * This can be used to write only a single state variable to the remote nodes,
 * instead of the enitre row, if only that variable has changed. To get the
 * correct offset and size, use `offsetof` and `sizeof`. For example, if the
 * Row type is `RowType` and the variable to write is `RowType::item`, use
 *
 *     sst_instance.put(offsetof(RowType, item), sizeof(item));
 *
 * If this SST is in Reads mode, this function does nothing.
 *
 * @param offset The offset, within the Row structure, of the region of the
 * row to write
 * @param size The number of bytes to write, starting at the offset.
 */
template<class Row, Mode ImplMode>
void SST<Row, ImplMode>::put(long long int offset, long long int size) {
    if (ImplMode == Mode::Writes) {
        for (int index = 0; index < num_members; ++index) {
            if (index == member_index) {
                // don't write to yourself!
                continue;
            }
            // perform a remote RDMA write on the owner of the row
            res_vec[index]->post_remote_write(offset, size);
        }
        // poll for one less than number of rows
        for (int index = 0; index < num_members - 1; ++index) {
            // poll for completion
            verbs_poll_completion();
        }
    }
}

} /* namespace sst */

#endif /* SST_H */
