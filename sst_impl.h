#ifndef SST_IMPL_H
#define SST_IMPL_H

//This will be included at the bottom of sst.h

#include <cassert>
#include <memory>
#include <utility>
#include <cstring>

#include "sst.h"
#include "predicates.h"
#include "tcp.h"

namespace sst {


template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::SST(const vector<int> &_members, int _node_rank) :
                members(_members.size()), num_members(_members.size()),
                table(new InternalRow[_members.size()]), res_vec(num_members),
                thread_shutdown(false),
					predicates(*(new Predicates())){

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
                        res_vec[sst_index] = std::make_unique<resources>(
                                node_rank, (char *) &(table[member_index]),
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
                        res_vec[sst_index] = std::make_unique<resources>(
                                node_rank, (char *) &(table[sst_index]),
                                (char *) &(table[member_index]),
                                sizeof(table[0]), sizeof(table[0]));
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
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::~SST() {
    thread_shutdown = true;
    delete &predicates;
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
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
volatile Row & SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::get(int index) {
    // check that the index is within range
    assert(index >= 0 && index < num_members);

    // return the table entry
    return table[index];
}

/**
 * Simply calls the get function.
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
volatile Row & SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::operator [](int index) {
    return get(index);
}

/**
 * @return The number of rows in the table.
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
int SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::get_num_rows() const {
    return num_members;
}

/**
 * This is the index of the local node, i.e. the node on which this code is
 * running, with respect to the group. `sst_instance[sst_instance.get_local_index()]`
 * will always returna reference to the local node's row.
 *
 * @return The index of the local row.
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
int SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::get_local_index() const {
    return member_index;
}

/**
 * This is a deep copy of the table that can be used for predicate evaluation,
 * which will no longer be affected by remote nodes updating their rows.
 *
 * @return A copy of all the SST's rows in their current state.
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
std::unique_ptr<typename SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::SST_Snapshot> SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::get_snapshot() const {
        return std::make_unique<SST_Snapshot>(table, num_members, named_functions);
}

/**
 * Exchanges a single byte of data with each member of the SST group over the
 * TCP (not RDMA) connection, in descending order of the members' node ranks.
 * This creates a synchronization barrier, since the TCP reads are blocking,
 * and should be called after SST initialization to ensure all nodes have
 * finished initializing their local SST code.
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
void SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::sync_with_members() const {
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
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
void SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::refresh_table() {
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
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
void SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::read() {
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
 * trigger functions for each predicate that fires. In addition, it 
 * continuously evaluates named functions one by one, and updates the local
 * row's observed values of those functions.
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
void SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::detect() {
    while (!thread_shutdown) {
        //Evaluate named functions
        util::for_each([&](auto named_fp, auto& value_slot){
            value_slot = (*named_fp)(*this);
        }, named_functions, table[get_local_index()].observed_values);

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
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
void SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::put() {
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
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
void SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::put(long long int offset, long long int size) {
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

//SST_Snapshot implementation


/**
 * @param _table A reference to the SST's current internal state table
 * @param _num_members The number of members (rows) in the SST
 * @param _named_functions A reference to the SST's list of named functions
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::SST_Snapshot::SST_Snapshot(
        const unique_ptr<volatile InternalRow[]>& _table, int _num_members,
        const typename NamedFunctionTypePack::function_types& _named_functions) :
        num_members(_num_members), table(new InternalRow[num_members]), named_functions(_named_functions) {

    std::memcpy(const_cast<InternalRow*>(table.get()),
            const_cast<const InternalRow*>(_table.get()),
            num_members * sizeof(InternalRow));
}

template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::SST_Snapshot::SST_Snapshot(
        const SST_Snapshot& to_copy) :
        num_members(to_copy.num_members), table(new InternalRow[num_members]), named_functions(
                to_copy.named_functions) {

    std::memcpy(table.get(), to_copy.table.get(),
            num_members * sizeof(InternalRow));
}


template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
const Row & SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::SST_Snapshot::get(int index) const {
    assert(index >= 0 && index < num_members);
    return table[index];
}

template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack>
const Row & SST<Row, ImplMode, NameEnum, NamedFunctionTypePack>::SST_Snapshot::operator[](int index) const {
    return get(index);
}

} /* namespace sst */

#endif /* SST_H */