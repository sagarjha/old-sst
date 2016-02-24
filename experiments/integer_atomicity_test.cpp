#include <iostream>
#include <fstream>
#include <vector>
#include <climits>

#include "../verbs.h"
#include "../tcp.h"

using std::vector;
using std::string;
using std::cin;
using std::cout;
using std::endl;
using std::ofstream;

using namespace sst;
using namespace sst::tcp;

void initialize(int num_nodes, int node_rank, const vector <string> & ip_addrs) {
  // initialize tcp connections
  tcp_initialize(num_nodes, node_rank, ip_addrs);
  
  // initialize the rdma resources
  verbs_initialize();
}

int main () {
  ofstream fout;
  fout.open("data_integer_atomicity_test.csv");

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

  int size = 2;
  int num_reruns = 1000000;
  
  if (node_rank == 0) {
    short int a = 0;
    short int b = SHRT_MAX;
    // create buffer for write and read
    char *write_buf, *read_buf;
    write_buf = (char*) &a;
    read_buf = (char *) malloc (4);

    int r_index = num_nodes-1-node_rank;
    resources *res = new resources (r_index, write_buf, read_buf, size, 4);

    while (true) {
      a = b-a;
    }

    free (read_buf);
    // important to de-register memory
    delete (res);
  }

  else {
    int b;
    // create buffer for write and read
    char *write_buf, *read_buf;
    write_buf = (char *) malloc (4);
    read_buf = (char *) &b;;
    
    int r_index = num_nodes-1-node_rank;
    resources *res = new resources (r_index, write_buf, read_buf, 4, size);
    
    for (int i = 0; i < num_reruns; ++i) {
      // index is 0, meaning that we start from the beginning and read the entire buffer
      res->post_remote_read (size);
      // poll for completion
      verbs_poll_completion();
      fout << b << endl;
    }

    // free malloc()ed area
    free (write_buf);
    // important to de-register memory
    delete (res);
  }

  
  verbs_destroy ();
  fout.close();
  return 0;
}
