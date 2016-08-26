#include <iostream>
#include <chrono>

#include "../sst.h"
#include "../tcp.h"

using namespace std;
using namespace sst;

struct Row {
    long long int time_in_nanoseconds;
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

    assert(num_nodes == 2);
    SST<Row> sst({0, 1}, node_id);
    sst[0].time_in_nanoseconds = -1;
    sst[1].time_in_nanoseconds = -1;
    sst.sync_with_members();

    int num_measurements = 10000;

    if(node_id == 0) {
        this_thread::sleep_for(5ms);
        struct timespec start_time, end_time;
        vector<long long int> skew(num_measurements);
        long long int sum_skew = 0;
        for(int i = 0; i < num_measurements; ++i) {
            clock_gettime(CLOCK_REALTIME, &start_time);
            sst[0].time_in_nanoseconds =
                (start_time.tv_sec * 1e9 + start_time.tv_nsec);
            sst.put();
            while(sst[1].time_in_nanoseconds < 0) {
            }
            clock_gettime(CLOCK_REALTIME, &end_time);
            long long int end_time_in_nanoseconds =
                (end_time.tv_sec * 1e9 + end_time.tv_nsec);
            skew[i] =
                (end_time_in_nanoseconds + sst[0].time_in_nanoseconds) / 2 -
                sst[1].time_in_nanoseconds;
            sum_skew += skew[i];
            sst[1].time_in_nanoseconds = -1;
        }
        for(int i = 0; i < num_measurements; ++i) {
            cout << skew[i] << endl;
        }

        cout << sum_skew / num_measurements << endl;
    } else {
        for(int i = 0; i < num_measurements; ++i) {
            while(sst[0].time_in_nanoseconds < 0) {
            }
            struct timespec start_time;
            clock_gettime(CLOCK_REALTIME, &start_time);
            sst[1].time_in_nanoseconds =
                (start_time.tv_sec * 1e9 + start_time.tv_nsec);
            sst[0].time_in_nanoseconds = -1;
            sst.put();
        }
    }
    sst.sync_with_members();
    return 0;
}
