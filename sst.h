#ifndef SST_H
#define SST_H

#include <cassert>
#include <functional>
#include <iostream>
#include <list>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <map>
#include <memory>

#include "predicates.h"
#include "verbs.h"
#include "tcp.h"

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

// common SST object
template<class Row>
class SST {
        // struct must be a pod. In addition, it should not contain any pointer types
        static_assert(std::is_pod<Row>::value, "Error! Row type must be POD.");

    protected:
        // list of members in the group (values are node ranks)
        vector<int> members;
        // list of node ranks mapped to member index
        map<int, int, std::greater<int>> members_by_rank;
        // number of members
        int num_members;
        // index of this node in the group
        int member_index;
        // the key value storage structure
        vector<Row> table;
        // flag to signal background threads to shut down
        bool thread_shutdown;

    public:
        // constructor for the state table
        SST(const vector<int> &_members, int _node_rank);
        virtual ~SST();
        // Access a local or remote row. Only the local row should be modified through this function.
        Row & get(int index);
        // [] operator for the SST
        Row & operator [](int index);
        // returns the number of rows
        int get_num_rows() const;
        // returns the index of this node in the group
        int get_local_index() const;
        // snapshot of the table
        const vector<Row> get_snapshot() const;
        // Does a TCP sync with each member of the SST
        void sync_with_members() const;
};

// SST-reads version
template<class Row>
class SST_reads: public SST<Row> {
        using SST<Row>::members;
        using SST<Row>::members_by_rank;
        using SST<Row>::num_members;
        using SST<Row>::member_index;
        using SST<Row>::table;
        using SST<Row>::thread_shutdown;

    private:
        // resources vector, one for each member
        vector<unique_ptr<resources>> res_vec;

    public:
        // takes care of the predicates
        Predicates_read<Row> predicates;

        // constructor for SST-reads
        SST_reads(const vector<int> &_members, int _node_rank);
        virtual ~SST_reads();
        // reads all the remote rows by RDMA
        void refresh_table();
        // refreshes continously
        void read();
        // detect predicates continuously
        void detect();
};

template<class Row>
class SST_writes: public SST<Row> {
        using SST<Row>::members;
        using SST<Row>::members_by_rank;
        using SST<Row>::num_members;
        using SST<Row>::member_index;
        using SST<Row>::table;
        using SST<Row>::thread_shutdown;

    private:
        // resources vector, one for each member
        vector<unique_ptr<resources>> res_vec;

    public:
        // takes care of the predicates
        Predicates_write<Row> predicates;

        // constructor for the SST-writes
        SST_writes(const vector<int> &_members, int _node_rank);
        virtual ~SST_writes();
        // write local row to all remote nodes
        void put();
        // write a contiguous area of row (in particular, a single state variable) to all remote nodes
        void put(long long int offset, long long int size);
        // detect predicates continuously
        void detect();
};

// initialize SST object
template<class Row>
SST<Row>::SST(const vector<int> & _members, int _node_rank) :
        members(_members.size()), num_members(_members.size()),
        table(_members.size()), thread_shutdown(false) {

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

template<class Row>
SST<Row>::~SST() {
    thread_shutdown = true;
}

// read a row (local or remote)
template<class Row>
Row & SST<Row>::get(int index) {
    // check that the index is within range
    assert(index >= 0 && index < num_members);

    // return the table entry
    return table[index];
}

// [] operator overloaded for the get function
template<class Row>
Row & SST<Row>::operator [](int index) {
    return get(index);
}

// returns the number of rows
template<class Row>
int SST<Row>::get_num_rows() const {
    return num_members;
}

// returns the index of the local row in the table
template<class Row>
int SST<Row>::get_local_index() const {
    return member_index;
}

// returns a copy of the table for predicate evaluation
template<class Row>
const vector<Row> SST<Row>::get_snapshot() const {
    // assignment does a deep copy
    vector<Row> ret = table;
    return ret;
}

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

// initializes the SST-read object
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

//destructor
template<class Row>
SST_reads<Row>::~SST_reads() {
}

// reads all the remote rows
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

// continuously refresh the table
template<class Row>
void SST_reads<Row>::read() {
    while (!thread_shutdown) {
        refresh_table();
    }
    cout << "Reader thread shutting down" << endl;
}

// evaluate predicates one by one continuously and run the triggers
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

// initializes the SST_writes object
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

//destructor
template<class Row>
SST_writes<Row>::~SST_writes() {
}

// write entire row to all the remote nodes
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

// write only a contiguous section of the row to all the remote nodes
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

// predicate detection for SST-writes
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
