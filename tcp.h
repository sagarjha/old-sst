#ifndef TCP_H
#define TCP_H

#include <vector>
#include <string>

namespace sst {

namespace tcp {

int get_sockets(int rank);
void tcp_initialize(int num_nodes, int node_rank, const std::vector <std::string> & ip_addrs);
void sync (int r_index);
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data);

} //namespace tcp

} //namespace sst

#endif
