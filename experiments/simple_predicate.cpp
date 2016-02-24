#include <iostream>
#include <thread>
#include <chrono>
#include <time.h>
#include <cstdlib>

#include "../sst.h"
#include "../tcp.h"
#include "statistics.h"
#include "timing.h"

using std::vector;
using std::cin;
using std::cout;
using std::endl;
using std::string;

using namespace sst;
using namespace sst::tcp;

struct Row {
  int a;
};

int main () {
  srand (time (NULL));
  // input number of nodes and the local node id
  int num_nodes, node_rank;
  cin >> num_nodes >> node_rank;

  // input the ip addresses
  vector <string> ip_addrs (num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    cin >> ip_addrs[i];
  }

  // initialize tcp connections
  tcp_initialize(num_nodes, node_rank, ip_addrs);
  
  // initialize the rdma resources
  verbs_initialize();
  
  // form a group with a subset of all the nodes
  vector <int> members (num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    members[i] = i;
  }
  
  // create a new shared state table with all the members
  SST_writes<Row> *sst = new SST_writes<Row> (members, node_rank);
  const int local = sst->get_local_index();

  // there are only 2 nodes; r_index is the index of the remote node
  int r_index = num_nodes-node_rank-1;
  (*sst)[local].a = 0;
  sst->put();
  // sync to make sure sst->a is 0 for both nodes
  sync (r_index);

  int num_times = 10000;
  vector <long long int> start_times (num_times), end_times (num_times);
  
  // start the experiment
  for (int i = 0; i < num_times; ++i) {
    // the predicate. Detects if the remote entry is greater than 0
    auto f = [r_index] (SST_writes <Row> *sst) {return (*sst)[r_index].a > 0;};
    
    // the initiator node
    if (node_rank == 0) {
	  // the trigger for the predicate. outputs time.
	  auto g = [&end_times, i] (SST_writes <Row> *sst) {
		  end_times[i] = experiments::get_realtime_clock();
	  };

      // register the predicate and the trigger
      sst->predicates.insert (f, g, PredicateType::ONE_TIME);

      // wait for random time
      long long int rand_time = (long long int) 2e6 + 1 + rand() % (long long int) 6e5;
	  experiments::busy_wait_for(rand_time);

      // start timer
      start_times[i] = experiments::get_realtime_clock();
      // set the integer
      (*sst)[local].a = 1;
      // update all nodes
      sst->put();
    }

    // the helper node
    else {
      // the trigger for the predicate. sets own entry in response
      auto g = [] (SST_writes <Row> *sst) {
		  (*sst)[sst->get_local_index()].a = 1;
		  sst->put();
      };

      // register the predicate and the trigger
      sst->predicates.insert (f, g);
    }

    // allow some time for detection
	experiments::busy_wait_for(1000000);

    sync (r_index);
    (*sst)[local].a =0;
    sst->put();
    // sync to make sure that both nodes are at this point
    sync (r_index);
  }

  if (node_rank == 0) {
    experiments::print_statistics (start_times, end_times, 2);
  }

  delete(sst);
  verbs_destroy();
  return 0;
}
