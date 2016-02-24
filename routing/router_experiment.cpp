#include <libio.h>
#include <stddef.h>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../experiments/statistics.h"
#include "../experiments/timing.h"
#include "../predicates.h"
#include "../sst.h"
#include "../tcp.h"
#include "../verbs.h"
#include "lsdb_row.h"
#include "routing.h"
#include "std_hashes.h"


using std::cout;
using std::endl;
using std::vector;
using std::ifstream;
using std::ofstream;
using std::string;
using std::unordered_set;
using std::pair;
using std::make_pair;
using std::tie;
using std::string;
using std::unique_ptr;

using sst::SST_writes;
using sst::experiments::LSDB_Row;
using sst::tcp::sync;


static const int TIMING_NODE = 0;
static int num_nodes, this_node_rank;

namespace sst {
namespace tcp {
extern int port;
}
}

void sync_with_all() {
	for (int n = 0; n < num_nodes; ++n) {
		if (n != this_node_rank) {
			sync(n);
		}
	}
}

int main (int argc, char** argv) {
  using namespace sst::experiments;
  if (argc < 3) {
    cout << "Please provide two config files: participating nodes and initial router state" << endl;
    return -1;
  }
  srand (time (NULL));

  ifstream node_config_stream;
  node_config_stream.open (argv[1]);

  // input number of nodes and the local node id
  node_config_stream >> num_nodes >> this_node_rank;

  // input the ip addresses
  vector <string> ip_addrs (num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    node_config_stream >> ip_addrs[i];
  }

  node_config_stream >> sst::tcp::port;

  node_config_stream.close();


  // initialize tcp connections
  sst::tcp::tcp_initialize(num_nodes, this_node_rank, ip_addrs);

  // initialize the rdma resources
  sst::verbs_initialize();

  // make all the nodes members of a group
  vector <int> group_members (num_nodes);
  for (int i = 0; i < num_nodes; ++i) {
    group_members[i] = i;
  }

  //Create the SST for link state
  SST_writes<LSDB_Row<RACK_SIZE>>* linkstate_sst = new SST_writes<LSDB_Row<RACK_SIZE>>(group_members, this_node_rank);
  int me = linkstate_sst->get_local_index();

  //set all routers to non-connected at first
  for(int i = 0; i < num_nodes; ++i) {
	  (*linkstate_sst)[me].link_cost[i] = -1;
  }
  //read list of connected routers and costs
  int nodenum, cost;
  ifstream router_config_stream;
  router_config_stream.open(argv[2]);

  while(router_config_stream.peek() != EOF) {
	  router_config_stream >> nodenum >> cost;
	  (*linkstate_sst)[me].link_cost[nodenum] = cost;

  }
  router_config_stream.close();

  //Set my own cost to 0, then wait for the other nodes to be ready
  (*linkstate_sst)[me].link_cost[this_node_rank] = 0;
  (*linkstate_sst)[me].barrier = 0;
  linkstate_sst->put();

  bool done = false;
  while (!done) {
	  done = true;
	  for (int i = 0; i < num_nodes; ++i) {
		  if ((*linkstate_sst)[i].link_cost[i] != 0) {
			  done = false;
		  }
	  }
  }
  sync_with_all();

  //Node-local state variables:
  vector<int> forwarding_table(num_nodes);
  unordered_set<pair<int, int>> links_used;
  unique_ptr<const LSDB_Row<RACK_SIZE>[]> linkstate_snapshot = linkstate_sst->get_snapshot();

//  const LSDB_Row<RACK_SIZE> & ref[] = linkstate_snapshot;
  //Compute initial routing table
  compute_routing_table(this_node_rank, num_nodes, forwarding_table, links_used, linkstate_snapshot.get());
//  print_routing_table(forwarding_table);

 /*
  //Simplest predicate: recompute if anything changes (this could be more targeted to only detect changes that matter)
  vector<unsigned long> last_timestamp(num_nodes);
  for(int i = 0; i < num_nodes; ++i) {
	  last_timestamp[i] = linkstate_sst[i].time_updated;
  }
  auto predicate = [&last_timestamp] (SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
	  for(int i = 0; i < num_nodes; ++i) {
		  if((*sst)[i].time_updated > last_timestamp[i]) {
			  return true;
		  }
	  }
	  return false;
  };
  

  //Action: Recompute my local routing table
  auto recompute_action = [&forwarding_table, &last_timestamp] (SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
	  auto linkstate_snapshot = sst->get_snapshot();
	  //First, save the new most-recent timestamps
	  for(int i = 0; i < num_nodes; ++i) {
		  last_timestamp[i] = linkstate_snapshot[i].time_updated;
	  }
	  compute_routing_table(forwarding_table, linkstate_snapshot);
	  cout << "Updated routing table: " << endl;
	  print_routing_table(forwarding_table);
  };

*/

  //Predicate: If any links change that might invalidate our existing path choices
  auto predicate = [&links_used, &linkstate_snapshot] (SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
	  for (int source = 0; source < num_nodes; ++source) {
		for (int target = 0; target < num_nodes; ++target) {
			auto link = make_pair(source, target);
			if(source != target && (
					//A link we used got worse (more costly) than its last known state
					(links_used.count(link) > 0 &&
							(*sst)[source].link_cost[target] > linkstate_snapshot[source].link_cost[target])
					//A link we didn't use got better (less costly) than its last known state
					|| (links_used.count(link) == 0 &&
							(*sst)[source].link_cost[target] < linkstate_snapshot[source].link_cost[target]))) {
//				cout << "Detected significant change: link (" << source << "," << target << ") changed from " <<  linkstate_snapshot[source].link_cost[target] << " to " << (*sst)[source].link_cost[target] << endl;
				return true;
			}
		}
	  }
	  return false;
  };

  //Action: Recompute my local routing table
  auto recompute_action = [&forwarding_table, &links_used, &linkstate_snapshot] (SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
	  linkstate_snapshot = sst->get_snapshot();
	  compute_routing_table(this_node_rank, num_nodes, forwarding_table, links_used, linkstate_snapshot.get());
	  //If the recompute was triggered by the experiment, not the reset...
	  if(linkstate_snapshot[0].link_cost[1] == 10) {
		  //Update the barrier
		  (*sst)[sst->get_local_index()].barrier++;
		  sst->put();

	  }
//	  cout << "Updated routing table: " << endl;
//	  print_routing_table(forwarding_table);
  };


  //Start the predicates
  linkstate_sst->predicates.insert(predicate, recompute_action, sst::PredicateType::RECURRENT);

  //Barrier before starting the experiment
  sync_with_all();
  //Warm up the processor again
  busy_wait_for(0.5 * SECONDS_TO_NS);

  //Node 0 is the "master" that starts each iteration of the experiment
  if(this_node_rank == TIMING_NODE) {

	  const int experiment_reps = 1000;
	  vector<long long int> start_times(experiment_reps),
			  first_complete_times(experiment_reps), end_times(experiment_reps);

	  //Predicate to detect all nodes reaching the barrier
	  int current_barrier_value = 1;
	  auto barrier_pred = [&current_barrier_value] (SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
		  for (int n = 0; n < num_nodes; ++n) {
			  if((*sst)[n].barrier < current_barrier_value)
				  return false;
		  }
		  return true;
	  };
  
	  std::uniform_int_distribution<long long int> wait_rand(10 * MILLIS_TO_NS, 20 * MILLIS_TO_NS);
	  std::mt19937 engine;
	  for(int rep = 0; rep < experiment_reps; ++rep) {

		  //Predicate to detect the first node reaching the barrier
		  auto first_done_pred = [&current_barrier_value](SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
			  //Don't count node 0, it will finish instantly because there's no network communication
			  for(int n = 1; n < num_nodes; ++n) {
				  if((*sst)[n].barrier == current_barrier_value) {
					  return true;
				  }
			  }
			  return false;
		  };
		  auto first_done_action = [&current_barrier_value, &first_complete_times, rep] (SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
			  first_complete_times[rep] = get_realtime_clock();
		  };
		  auto barrier_action = [&current_barrier_value, &end_times, rep] (SST_writes<LSDB_Row<RACK_SIZE>>* sst) {
			  end_times[rep] = get_realtime_clock();
//			  cout << "All nodes have reached barrier " << current_barrier_value << endl;
			  current_barrier_value++;
			  //Reset link values to initial state
			  int local = sst->get_local_index();
			  (*sst)[local].link_cost[1] = 1;
			  (*sst)[local].link_cost[2] = 1;
			  sst->put();
		  };
		  //Launch predicates to monitor for the experiment completing
		  linkstate_sst->predicates.insert(first_done_pred, first_done_action, sst::PredicateType::ONE_TIME);
		  linkstate_sst->predicates.insert(barrier_pred, barrier_action, sst::PredicateType::ONE_TIME);

		  //Wait a random amount of time, then change connection metrics
		  long long int rand_time = wait_rand(engine);
		  busy_wait_for(rand_time);

//		  cout << "Starting experiment trial " << rep << endl;
		  start_times[rep] = get_realtime_clock();
		  //Make a change that's guaranteed to make everyone recompute their routing tables,
		  //by making two of 0's links very slow
		  (*linkstate_sst)[me].link_cost[1] = 10;
		  //Workaround for the edge-case of 3 nodes
		  if(num_nodes == 3)
			  (*linkstate_sst)[me].link_cost[2] = 5;
		  else
			  (*linkstate_sst)[me].link_cost[2] = 10;
		  linkstate_sst->put();
		  //wait for end-of-experiment reset
		  while((*linkstate_sst)[me].link_cost[1] == 10) {

		  }
		  // wait a while for the reset link table to propagate
		  busy_wait_for(1 * MILLIS_TO_NS);

	  }

	  //Sync with the other nodes to let them finish
	  sync_with_all();

	  double first_mean, first_stdev, whole_mean, whole_stdev;
	  tie(whole_mean, whole_stdev) = compute_statistics(start_times, end_times);
	  tie(first_mean, first_stdev) = compute_statistics(start_times, first_complete_times);
	  print_statistics(start_times, end_times);
	  ofstream data_out_stream(string("router_results.csv").c_str(), ofstream::app);
	  data_out_stream << num_nodes << "," << first_mean << "," << first_stdev << "," << whole_mean << "," << whole_stdev << endl;
	  data_out_stream.close();

	  vector<double> all_times = timestamps_to_elapsed(start_times, end_times);
	  vector<double> all_first_times = timestamps_to_elapsed(start_times, first_complete_times);
	  ofstream data_stream_2(string("all_times_" + std::to_string(num_nodes)).c_str());
	  for(size_t i = 0; i < all_times.size(); ++i) {
		  data_stream_2 << all_first_times[i] << "," << all_times[i] << endl;
	  }
	  data_stream_2.close();
  }
  //All other nodes should just wait for the experiment to end, and let their routing predicates get triggered
  else {

	  //This will block until all trials are complete
	  sync(TIMING_NODE);
  }
//		  //Pick a random connection to change
//		  vector<int> connected_nodes;
//		  for(int i = 0; i < num_nodes; ++i) {
//			  if(linkstate_sst->link_cost[i] > 0) {
//				  connected_nodes.push_back(i);
//			  }
//		  }
//		  std::random_shuffle(connected_nodes.begin(), connected_nodes.end());
//		  int node_to_change = connected_nodes.front();
//		  //Pick a random new metric for this link
//		  linkstate_sst->link_cost[node_to_change] = metric_rand(engine);


  delete(linkstate_sst);
  return 0;
}

