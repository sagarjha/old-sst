#include <iostream>
#include <cassert>
#include <functional>
#include <memory>
#include <vector>

#include "../sst.h"
#include "../tcp.h"

using namespace std;
using namespace sst;

template <uint32_t max_msg_size>
struct Message {
    char buf[max_msg_size];
    uint64_t seq;
};

template <uint32_t window_size, uint32_t max_msg_size>
struct Row {
    Message<max_msg_size> slots[window_size];
    uint64_t num_received;
};

typedef function<void(uint64_t, volatile char*)> receiver_callback_t;

template <uint32_t window_size, uint32_t max_msg_size>
class group {
    // number of messages for which get_buffer has been called
    uint64_t num_queued = 0;
    // number of messages for which RDMA write is complete
    uint64_t num_sent = 0;
    // number of messages received
    uint64_t num_received = 0;
    // the number of messages acknowledged by all the nodes
    uint64_t num_multicasts_finished = 0;
    // rank of the node in the sst
    uint32_t my_rank;
    // only one send at a time
    mutex msg_send_mutex;

    receiver_callback_t receiver_callback;

    using SST_type = SST<Row<window_size, max_msg_size>>;

    // SST
    unique_ptr<SST_type> multicastSST;

    void register_predicates() {
        auto receiver_pred = [this](const SST_type& sst) {
            uint32_t slot = num_received % window_size;
            if(sst[0].slots[slot].seq == num_received / window_size) {
                return true;
            }
            return false;
        };
        auto receiver_trig = [this](SST_type& sst) {
            uint32_t slot = num_received % window_size;
            this->receiver_callback(num_received, sst[0].slots[slot].buf);
            num_received++;
        };
        multicastSST->predicates.insert(receiver_pred, receiver_trig,
                                        PredicateType::RECURRENT);

        // only for the sender
        auto update_finished_multicasts_pred =
            [this](const SST_type& sst) { return true; };
        auto update_finished_multicasts_trig = [this](SST_type& sst) {
            uint64_t min_multicast_num = sst[0].num_received;
            uint32_t num_members = sst.get_num_rows();
            for(uint32_t i = 1; i < num_members; ++i) {
                if(sst[i].num_received < min_multicast_num) {
                    min_multicast_num = sst[i].num_received;
                }
            }
            num_multicasts_finished = min_multicast_num;
        };
        if(my_rank == 0) {
            multicastSST->predicates.insert(update_finished_multicasts_pred,
                                            update_finished_multicasts_trig,
                                            sst::PredicateType::RECURRENT);
        }
    }

public:
    group(vector<uint> members, uint32_t my_id,
          receiver_callback_t receiver_callback)
        : receiver_callback(receiver_callback) {
        size_t num_members = members.size();
        for(uint32_t i = 0; i < num_members; ++i) {
            if(members[i] == my_id) {
                my_rank = i;
                break;
            }
        }
        multicastSST = make_unique<SST_type>(members, my_rank);
	register_predicates();
    }

    char* get_buffer(uint32_t msg_size) {
        lock_guard<mutex> lock(msg_send_mutex);
        assert(my_rank == 0);
        assert(msg_size <= max_msg_size);
        if(num_queued - num_multicasts_finished < window_size) {
            uint32_t slot = num_queued % window_size;
            num_queued++;
            return (*multicastSST)[0].slots[slot].buf;
        }
        return nullptr;
    }

    void send() {
        assert(my_rank == 0);
        uint32_t slot = num_sent % window_size;
        num_sent++;
        (*multicastSST)[0].slots[slot].seq++;
        // multicastSST->put(offsetof(Row<window_size, max_msg_size>, slots) +
        // slot * sizeof(Message),
        //          sizeof(Message));
    }
};

int main() { group<10, 1000> g({0, 1, 2}, 0, nullptr); }
