#include <iostream>
#include <cassert>
#include <functional>
#include <map>
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

    void initialize() {
        uint32_t num_members = multicastSST->get_num_rows();
        for(uint i = 0; i < num_members; ++i) {
            (*multicastSST)[i].num_received = 0;
            for(uint j = 0; j < window_size; ++j) {
                (*multicastSST)[i].slots[j].seq = 0;
            }
        }
	multicastSST->sync_with_members();
    }

    void register_predicates() {
        auto receiver_pred = [this](const SST_type& sst) {
            uint32_t slot = sst[my_rank].num_received % window_size;
            if(sst[0].slots[slot].seq == sst[my_rank].num_received / window_size) {
                return true;
            }
            return false;
        };
        auto receiver_trig = [this](SST_type& sst) {
            uint32_t slot = sst[my_rank].num_received % window_size;
            this->receiver_callback(sst[my_rank].num_received, sst[0].slots[slot].buf);
            sst[my_rank].num_received++;
            sst.put((char*)addressof(sst[0].num_received) -
                        (char*)addressof(sst[0]),
                    sizeof(sst[0].num_received));
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
	initialize();
	register_predicates();
    }

    volatile char* get_buffer(uint32_t msg_size) {
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
        multicastSST->put(
            (char*)addressof((*multicastSST)[0].slots[slot]) -
	    (char*)addressof((*multicastSST)[0]),
            sizeof(Message<max_msg_size>));
    }
};

int main() {
    // input number of nodes and the local node id
    uint32_t node_id, num_nodes;
    cin >> node_id >> num_nodes;

    // input the ip addresses
    map<uint32_t, string> ip_addrs;
    for(unsigned int i = 0; i < num_nodes; ++i) {
        cin >> ip_addrs[i];
    }

    // initialize tcp connections
    tcp::tcp_initialize(node_id, ip_addrs);

    // initialize the rdma resources
    verbs_initialize();

    vector<uint32_t> members(num_nodes);
    for(uint i = 0; i < num_nodes; ++i) {
        members[i] = i;
    }

    group<10, 1000> g(members, node_id, [](uint64_t index, volatile char* msg) {
        cout << "Index is: " << index << endl;
        cout << "Message is: ";
        for(int i = 0; i < 50; ++i) {
            cout << msg[i];
        }
        cout << endl;
    });
    if(node_id == 0) {
        for(int i = 0; i < 10; ++i) {
            volatile char* buf;
            while((buf = g.get_buffer(50)) == NULL) {
            }
            for(int i = 0; i < 50; ++i) {
                buf[i] = 'a' + (i % 26);
            }
            // for(int i = 0; i < 50; ++i) {
            //     cout << buf[i];
            // }
            // cout << endl;
            g.send();
        }
    }
    while(true) {
    }
}
