#include <iostream>
#include <fstream>
#include <sstream>

#include "multicast.h"
#include "../../max_members.h"

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

    vector <unsigned long long int> times(MAX_MEMBERS);
    uint num_finished = 0;
    struct timespec time;
    group<window_size, max_msg_size, MAX_MEMBERS> g(
        members, node_id,
        [&times, &time, &num_finished, &num_nodes, &num_messages](
            uint32_t sender_rank, uint64_t index, volatile char* msg,
            uint32_t size) {
            // start timer
            clock_gettime(CLOCK_REALTIME, &time);
	    times[sender_rank] += time.tv_sec*1e9 + time.tv_nsec;
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
    for (uint i = 0; i < MAX_MEMBERS; ++i) {
      stringstream ss;
      ss << MAX_MEMBERS << "_" << i;
      ofstream fout;
      fout.open(ss.str());
      fout << times[i] << endl;
      fout.close();
    }
    // sync before destroying resources
    char temp_char;
    char tQ[2] = {'Q', 0};
    tcp::sock_sync_data(tcp::get_socket(1-node_id), 1, tQ, &temp_char);
}
