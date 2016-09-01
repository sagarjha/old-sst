#include <iostream>
#include <fstream>
#include <sstream>

#include "multicast.h"
#include "../../max_members.h"
#include "time_skew.h"

using namespace std;
using namespace sst;

volatile bool done = false;

int main() {
    constexpr uint max_msg_size = 1, window_size = 1000;
    const unsigned int num_messages = 1000000;
    // input number of nodes and the local node id
    uint32_t node_id, num_nodes;
    cin >> node_id >> num_nodes;

    assert(MAX_MEMBERS == num_nodes);

    // input the ip addresses
    map<uint32_t, string> ip_addrs;
    for(unsigned int i = 0; i < num_nodes; ++i) {
        cin >> ip_addrs[i];
    }

    // initialize tcp connections
    tcp::tcp_initialize(node_id, ip_addrs);

    // initialize the rdma resources
    verbs_initialize();

    std::vector<uint32_t> members(num_nodes);
    for(uint i = 0; i < num_nodes; ++i) {
        members[i] = i;
    }

    vector<vector<long long int>> times(MAX_MEMBERS);
    for(uint i = 0; i < MAX_MEMBERS; ++i) {
        times[i].resize(num_messages);
    }
    uint num_finished = 0;
    struct timespec time;
    group<window_size, max_msg_size, MAX_MEMBERS> g(
        members, node_id,
        [&times, &time, &num_finished, &num_nodes, &num_messages](
            uint32_t sender_rank, uint64_t index, volatile char* msg,
            uint32_t size) {
            // start timer
            clock_gettime(CLOCK_REALTIME, &time);
            times[sender_rank][index] = time.tv_sec * 1e9 + time.tv_nsec;
            if(index == num_messages - 1) {
                num_finished++;
            }
            if(num_finished == num_nodes) {
                done = true;
            }
        });
    for(uint i = 0; i < num_messages; ++i) {
        volatile char* buf;
        while((buf = g.get_buffer(max_msg_size)) == NULL) {
        }
        g.send();
    }
    while(!done) {
    }
    uint64_t skew = 0;
    if(node_id != 0) {
        SST<TimeRow> time_sst({0, node_id}, node_id);
        time_sst[0].time_in_nanoseconds = -1;
        time_sst[1].time_in_nanoseconds = -1;
        time_sst.sync_with_members();
        skew = server(time_sst, 1, 0);
        time_sst.sync_with_members();
    }
    else {
      for (uint i = 1; i < num_nodes; ++i) {
          SST<TimeRow> time_sst({0, i}, 0);
          time_sst[0].time_in_nanoseconds = -1;
          time_sst[1].time_in_nanoseconds = -1;
          time_sst.sync_with_members();
          client(time_sst, 0, 1);
          time_sst.sync_with_members();
      }
    }
    
    for(uint i = 0; i < MAX_MEMBERS; ++i) {
        stringstream ss;
        ss << MAX_MEMBERS << "_" << i;
        ofstream fout;
        fout.open(ss.str());
        for(uint j = 0; j < num_messages; ++j) {
            fout << times[i][j]-skew << endl;
        }
        fout.close();
    }

    cout << "skew is " << skew << endl;

    for(uint i = 0; i < num_nodes; ++i) {
        if(i == node_id) {
            continue;
        }
        char temp_char;
        char tQ[2] = {'Q', 0};
	tcp::sock_sync_data(tcp::get_socket(i), 1, tQ, &temp_char);
    }
}
