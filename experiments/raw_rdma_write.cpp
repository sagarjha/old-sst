#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <time.h>
#include <cstdlib>

#include "../verbs.h"
#include "../tcp.h"
#include "statistics.h"

using std::vector;
using std::string;
using std::cin;
using std::cout;
using std::endl;

using namespace sst;
using namespace sst::tcp;

void initialize(int num_nodes, int node_rank, const vector <string> & ip_addrs) {
  // initialize tcp connections
  tcp_initialize(num_nodes, node_rank, ip_addrs);
  
  // initialize the rdma resources
  verbs_initialize();
}

int main () {
  srand (time (NULL));
  // input number of nodes and the local node id
  int num_nodes, node_rank;
  cin >> num_nodes;
  cin >> node_rank;

  // input the ip addresses
  vector <string> ip_addrs (num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    cin >> ip_addrs[i];
  }

  // create all tcp connections and initialize global rdma resources
  initialize(num_nodes, node_rank, ip_addrs);
  
  int a;
  volatile int b;
  a=b=0;
  // create read and write buffers
  char *write_buf = (char*)&a;
  char *read_buf = (char*)&b;

  int r_index = num_nodes-1-node_rank;
  
  // create the rdma struct for exchanging data
  resources *res = new resources (r_index, read_buf, write_buf, sizeof(int), sizeof(int));

  int num_times = 10000;
  vector <long long int> start_times (num_times), end_times (num_times);

  for (int rep = 0; rep < num_times; ++rep) {
    if (node_rank == 0) {
      // wait for random time
      long long int rand_time = (long long int) 2e5 + 1 + rand() % (long long int) 6e5;
      for (long long int i = 0; i < rand_time; ++i) {
	
      }
      
      struct timespec start_time, end_time;
      // start timer
      clock_gettime(CLOCK_REALTIME, &start_time);
      start_times[rep] = start_time.tv_sec*(long long int) 1e9 + start_time.tv_nsec;
      a = 1;
      res->post_remote_write (sizeof(int));
      while (b == 0) {
      }
      clock_gettime(CLOCK_REALTIME, &end_time);
      end_times[rep] = end_time.tv_sec*(long long int) 1e9 + end_time.tv_nsec;
      verbs_poll_completion ();
      sync(r_index);
      a = b = 0;
    }
    
    else {
      while (b == 0) {
      }
      a = 1;
      res->post_remote_write (sizeof(int));
      verbs_poll_completion ();
      sync(r_index);
      a = b = 0;
    }
  }

  if (node_rank == 0) {
    experiments::print_statistics (start_times, end_times, 2);
  }

  delete (res);
  verbs_destroy ();
  
  return 0;
}
