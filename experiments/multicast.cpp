#include <iostream>
#include <map>
#include <fstream>
#include <ctime>
#include <cstdlib>
#include <cassert>
#include <memory>
#include <unique_lock>

#include "../sst.h"
#include "../tcp.h"
//Since all SST instances are named sst, we can use this convenient hack
#define LOCAL sst.get_local_index()

using namespace std;

template<uint window_size, uint max_msg_size>
class group {
  struct Message {
    char buf[max_msg_size];
    uint seq;
  };
  struct DataRow {
    Message slots[window_size];
  };

  struct controlRow {
    uint num_sent;
    uint num_recvd;
  }
  
  // number of messages for which RDMA write is complete
  uint num_sent = 0;
  // the number of messages acknowledged by all the nodes
  uint num_multicasts_finished = 0;
  // sender node
  uint sender_id;
  // node id of the current node
  uint my_id;
  // only one send at a time
  mutex msg_send_mutex;
  
  // SST
  SST<Row, Mode::Writes> sst = SST<Row, Mode::Writes> (members, node_rank);
  
public:
  char* send(char* msg, uint msg_size) {
    lock_guard<mutex> lock(msg_send_mutex);
    assert(sender_id == my_id);
    assert(msg_size <= max_msg_size);
    while (num_sent - num_multicasts_finished == window_size) {
      uint slot = num_sent%window_size;
      sst.put();
    }
  }
};
