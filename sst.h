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

template<class Row>
class SST_reads;
template<class Row>
class SST_writes;

/**
 * The base SST object, containing common members and code.
 */
template<class Row>
class SST {
        // struct must be a pod. In addition, it should not contain any pointer types
        static_assert(std::is_pod<Row>::value, "Error! Row type must be POD.");

    protected:
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
        /** A flag to signal background threads to shut down; set to true during destructor calls. */
        bool thread_shutdown;

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
        /** Does a TCP sync with each member of the SST. */
        void sync_with_members() const;
};

/** 
 * The "reads" version of the SST.
 */
template<class Row>
class SST_reads: public SST<Row> {
        using SST<Row>::members;
        using SST<Row>::members_by_rank;
        using SST<Row>::num_members;
        using SST<Row>::member_index;
        using SST<Row>::table;
        using SST<Row>::thread_shutdown;

    private:
        /** RDMA resources vector, one for each member. */
        vector<unique_ptr<resources>> res_vec;

    public:
        /** Predicate management object for this SST. */
        Predicates_read<Row> predicates;

        SST_reads(const vector<int> &_members, int _node_rank);
        virtual ~SST_reads();
        /** Reads all the remote rows once by RDMA. */
        void refresh_table();
        /** Continuously refreshes all the remote rows. */
        void read();
        /** Continuously evaluates predicates to detect when they become true. */
        void detect();
};

/**
 * The "writes" versions of the SST.
 */
template<class Row>
class SST_writes: public SST<Row> {
        using SST<Row>::members;
        using SST<Row>::members_by_rank;
        using SST<Row>::num_members;
        using SST<Row>::member_index;
        using SST<Row>::table;
        using SST<Row>::thread_shutdown;

    private:
        /** RDMA resources vector, one for each member. */
        vector<unique_ptr<resources>> res_vec;

    public:
        /** Predicate management object for this SST. */
        Predicates_write<Row> predicates;

        /** Constructs an SST-writes instance. */
        SST_writes(const vector<int> &_members, int _node_rank);
        virtual ~SST_writes();
        /** Writes the local row to all remote nodes. */
        void put();
        /** Writes a contiguous subset of the local row to all remote nodes. */
        void put(long long int offset, long long int size);
        /** Continuously evaluates predicates to detect when they become true. */
        void detect();
};

/** 
 * Base constructor for the state table; initializes parts of the SST object
 * that do not depend on whether we are using reads or writes.
 *
 * @param _members A vector of node ranks (IDs), each of which represents a
 * node participating in the SST. The order of nodes in this vector is the
 * order in which their rows will appear in the SST.
 * @param _node_rank The node rank of the local node, i.e. the one on which
 * this code is running.
 */
template<class Row>
SST<Row>::SST(const vector<int> & _members, int _node_rank) :
        members(_members.size()), num_members(_members.size()),
        table(new Row[_members.size()]), thread_shutdown(false) {

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

}

/**
 * Base destructor for the state table; sets thread_shutdown to true so that
 * detached background threads exit cleanly.
 */
template<class Row>
SST<Row>::~SST() {
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
template<class Row>
volatile Row & SST<Row>::get(int index) {
    // check that the index is within range
    assert(index >= 0 && index < num_members);

    // return the table entry
    return table[index];
}

/**
 * Simply calls the get function.
 */
template<class Row>
volatile Row & SST<Row>::operator [](int index) {
    return get(index);
}

/**
 * @return The number of rows in the table.
 */
template<class Row>
int SST<Row>::get_num_rows() const {
    return num_members;
}

/**
 * This is the index of the local node, i.e. the node on which this code is
 * running, with respect to the group. `sst_instance[sst_instance.get_local_index()]`
 * will always returna reference to the local node's row.
 *
 * @return The index of the local row.
 */
template<class Row>
int SST<Row>::get_local_index() const {
    return member_index;
}

/**
 * This is a deep copy of the table that can be used for predicate evaluation,
 * which will no longer be affected by remote nodes updating their rows.
 *
 * @return A copy of all the SST's rows in their current state.
 */
template<class Row>
unique_ptr<const Row[]> SST<Row>::get_snapshot() const {
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
template<class Row>
void SST<Row>::sync_with_members() const {
    int node_rank, sst_index;
    for (auto const& rank_index : members_by_rank) {
        std::tie(node_rank, sst_index) = rank_index;
        if (sst_index != member_index) {
            tcp::sync(node_rank);
        }
    }
}

/**
 * Constructs an SST-reads instance, initializes RDMA resources, and spawns
 * background threads.
 *
 * @copydetails SST::SST()
 */
template<class Row>
SST_reads<Row>::SST_reads(const vector<int> & _members, int _node_rank) :
        SST<Row>(_members, _node_rank), res_vec(num_members) {

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
    thread reader(&SST_reads::read, this);
    thread detector(&SST_reads::detect, this);
    reader.detach();
    detector.detach();

    cout << "Initialized SST and Started Threads" << endl;
}

/**
 * Destructor. Currently empty. All cleanup is automatic.
 */
template<class Row>
SST_reads<Row>::~SST_reads() {
}

/**
 *
 */
template<class Row>
void SST_reads<Row>::refresh_table() {
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

/**
 * This function is run in a detached background thread to continuously keep
 * the local SST table updated.
 */
template<class Row>
void SST_reads<Row>::read() {
    while (!thread_shutdown) {
        refresh_table();
    }
    cout << "Reader thread shutting down" << endl;
}

/**
 * This function is run in a detached background thread to detect predicate
 * events. It continuously evaluates predicates one by one, and runs the
 * trigger functions for each predicate that fires.
 */
template<class Row>
void SST_reads<Row>::detect() {
    while (!thread_shutdown) {
        // one time predicates need to be evaluated only until they become true
        auto pred_it = predicates.one_time_predicates.begin();
        while (pred_it != predicates.one_time_predicates.end()) {
            if (pred_it->first(this) == true) {
                for (auto func : pred_it->second) {
                    func(this);
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
            if (pred_it->first(this) == true) {
                for (auto func : pred_it->second) {
                    func(this);
                }
            }
        }

        // transition predicates are only evaluated when they change from false to true
        pred_it = predicates.transition_predicates.begin();
        auto pred_state_it = predicates.transition_predicate_states.begin();
        while (pred_it != predicates.transition_predicates.end()) {
            //*pred_state_it is the previous state of the predicate at *pred_it
            bool curr_pred_state = pred_it->first(this);
            if (curr_pred_state == true && *pred_state_it == false) {
                for (auto func : pred_it->second) {
                    func(this);
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
 * Constructs an SST-writes instance, initializes RDMA resources, and spawns
 * background threads.
 *
 * @copydetails SST::SST()
 */
template<class Row>
SST_writes<Row>::SST_writes(const vector<int> & _members, int _node_rank) :
        SST<Row>(_members, _node_rank), res_vec(num_members) {

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

    thread detector(&SST_writes::detect, this);
    detector.detach();

    cout << "Initialized SST and Started Threads" << endl;
}

/**
 * Destructor. Currently empty. All cleanup is automatic.
 */
template<class Row>
SST_writes<Row>::~SST_writes() {
}

/**
 * This writes the entire local row, using a one-sided RDMA write, to all of
 * the other members of the SST group.
 */
template<class Row>
void SST_writes<Row>::put() {
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

/**
 * This can be used to write only a single state variable to the remote nodes,
 * instead of the enitre row, if only that variable has changed. To get the
 * correct offset and size, use `offsetof` and `sizeof`. For example, if the
 * Row type is `RowType` and the variable to write is `RowType::item`, use
 *
 *     sst_instance.put(offsetof(RowType, item), sizeof(item));
 *
 *
 * @param offset The offset, within the Row structure, of the region of the
 * row to write
 * @param size The number of bytes to write, starting at the offset.
 */
template<class Row>
void SST_writes<Row>::put(long long int offset, long long int size) {
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

/**
 * @copydetails SST_reads::detect()
 */
template<class Row>
void SST_writes<Row>::detect() {
    while (!thread_shutdown) {
        // one time predicates need to be evaluated only until they become true
        auto pred_it = predicates.one_time_predicates.begin();
        while (pred_it != predicates.one_time_predicates.end()) {
            if (pred_it->first(this) == true) {
                for (auto func : pred_it->second) {
                    func(this);
                }
                pred_it = predicates.one_time_predicates.erase(pred_it);
            } else {
                pred_it++;
            }
        }

        // recurrent predicates are evaluated each time they are found to be true
        for (pred_it = predicates.recurrent_predicates.begin();
                pred_it != predicates.recurrent_predicates.end(); ++pred_it) {
            if (pred_it->first(this) == true) {
                for (auto func : pred_it->second) {
                    func(this);
                }
            }
        }

        // transition predicates are only evaluated when they change from false to true
        pred_it = predicates.transition_predicates.begin();
        auto pred_state_it = predicates.transition_predicate_states.begin();
        while (pred_it != predicates.transition_predicates.end()) {
            //*pred_state_it is the previous state of the predicate at *pred_it
            bool curr_pred_state = pred_it->first(this);
            if (curr_pred_state == true && *pred_state_it == false) {
                for (auto func : pred_it->second) {
                    func(this);
                }
            }
            *pred_state_it = curr_pred_state;

            ++pred_it;
            ++pred_state_it;
        }
    }
    cout << "Predicate detection thread shutting down" << endl;
}

} /* namespace sst */

#endif /* SST_WRITE_H */
