/**
 * @file tcp.cpp
 * Implementation of the TCP subsystem of SST, which is used for out-of-band
 * communications between nodes.
 */
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <infiniband/verbs.h>

#include "tcp.h"

using std::vector;
using std::endl;
using std::cout;
using std::pair;
using std::string;

namespace sst {

namespace tcp {

/** The port to use for the TCP connections. */
int port = 25551;
/** The IP address of the local node. */
string ip_addr;
/** Socket handles for each of the remote nodes. */
vector <int> sockets;

/**
 * Prints an error message (using perror()) and exits the program.
 * @param msg The error message to print.
 */
void error(const char *msg)
{
    perror(msg);
    exit(1);
}

/**
 * @param rank The node rank of the desired node.
 * @return The socket file descriptor for the TCP socket to that node.
 */
int get_socket (int rank) {
  return sockets[rank];
}

/**
 * @param r_index The node rank of the node to exchange data with.
 */
void sync (int r_index) {
  char  temp_char; 
  char tQ[2] = {'Q', 0};
  sock_sync_data(get_socket (r_index), 1, tQ, &temp_char);
}

/**
 * @details
 * This will first send data to a remote node using the socket, then wait for
 * the remote node to send the same amount of data back.
 *
 * @param sock The file descriptor of the socket to use for the remote connection
 * @param xfer_size The number of bytes of data to exchange.
 * @param local_data A buffer containing the data to send to the remote node;
 * must be at least `xfer_size` bytes large.
 * @param remote_data A buffer into which the remote node's response data will
 * be written; must be at least `xfer_size` bytes large.
 * @return The return code of the read() call that received data from the
 * remote node.
 */
int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data) {
  int rc;
  int read_bytes = 0;
  int total_read_bytes = 0;
  rc = write(sock, local_data, xfer_size);
  if(rc < xfer_size)
    cout << "Failed writing data during sock_sync_data\n";
  else
    rc = 0;
  while(!rc && total_read_bytes < xfer_size) {
    read_bytes = read(sock, remote_data, xfer_size);
    if(read_bytes > 0)
      total_read_bytes += read_bytes;
    else
      rc = read_bytes;
  }
  return rc;
}

/**
 * Exchanges node ranks with another node over a TCP socket.
 *
 * @param sock The TCP socket the remote node is connected on.
 * @param node_rank The node rank of this node (the local node).
 * @return The node rank of the remote node.
 */
static int exchange_node_rank(int sock, int node_rank) {
  char msg[10];
  sprintf(msg, "%d", node_rank);
  write(sock, msg, sizeof(msg));
  read(sock, msg, sizeof(msg));
  int rank;
  sscanf(msg, "%d", &rank);
  return rank;
}

/**
 * Sets up a TCP socket to listen on (aka a "server socket").
 *
 * @return The TCP listening socket.
 */
int tcp_listen () {
  int listenfd, pid;
  struct sockaddr_in serv_addr;

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
    error("ERROR opening socket");

  int reuse_addr = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_addr,
             sizeof(reuse_addr));

  bzero((char *)&serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    error("ERROR on binding");
  listen(listenfd, 1);
  return listenfd;
}

/**
 * Accepts a TCP connection from a remote node by listening on the given socket
 * until the node connects.
 * 
 * @param listenfd The file descriptor of the socket to listen on
 * @param num_nodes The number of nodes in the %SST system.
 * @param node_rank The node rank of this node.
 * @return A pair containing (1) the socket for the new TCP connection and (2)
 * the node rank of the remote node that connected on this socket.
 */
pair <int, int> tcp_accept (int listenfd, int num_nodes, int node_rank) {
  int sock = accept(listenfd, NULL, 0);
  int rank = exchange_node_rank(sock, node_rank);
  return pair<int, int>(sock, rank);
}

pair <int, int> tcp_connect (const char* servername, int num_nodes, int node_rank) {
  int sock;
  struct sockaddr_in serv_addr;
  struct hostent *server;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) error("ERROR opening socket");
  server = gethostbyname(servername);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host\n");
    exit(0);
  }
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
  serv_addr.sin_port = htons(port);

  while (connect(sock,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
  }

  int rank = exchange_node_rank(sock, node_rank);
  return pair<int, int>(sock, rank);
}

/**
 * Establishes TCP connections with all the nodes whose IP addresses are specified.
 *
 * @param ip_addrs A vector of the IP addresses of the nodes in the system,
 * indexed by node rank. Its size must be equal to `num_nodes`.
 * @param num_nodes The number of nodes in the %SST system.
 * @param node_rank The rank of the local node, i.e. the node on which this
 * code is running.
 */
void establish_tcp_connections (const vector <string> & ip_addrs, int num_nodes, int node_rank) {
  // try to connect to nodes greater than node_rank
  for(int i = num_nodes - 1; i > node_rank; --i) {
    cout << "trying to connect to node rank " << i << endl;
    pair <int, int> p = tcp_connect(ip_addrs[i].c_str(), num_nodes, node_rank);
    cout << "connected to node rank " << i << endl << endl;
    assert(p.second == i);
    // set the socket
    sockets[i] = p.first;
  }

  // set up listen on the port
  int listenfd = tcp_listen ();

  // now accept connections
  // make sure that the caller is correctly identified with its id!
  for(int i = node_rank - 1; i >= 0; --i) {
    cout << "waiting for nodes with lesser rank" << endl;
    pair <int, int> p = tcp_accept(listenfd, num_nodes, node_rank);
    cout << "connected to node rank " << p.second << endl << endl;
    assert(p.second < node_rank);
    // set the socket
    sockets[p.second] = p.first;
  }

  // close the listening socket
  close (listenfd);
}

/**
 * @details
 * This initializes static global variables, so it should only be called once
 * even if multiple SST objects are created.
 *
 * @param num_nodes The number of nodes in the %SST system.
 * @param node_rank The rank of the local node, i.e. the node on which this
 * code is running.
 * @param ip_addrs A vector of the IP addresses of the nodes in the system,
 * indexed by node rank. Its size must be equal to `num_nodes`.
 */
void tcp_initialize(int num_nodes, int node_rank, const vector <string> & ip_addrs) {
  ip_addr = ip_addrs[node_rank];
  sockets.resize(num_nodes);
  
  // connect all the nodes via tcp
  establish_tcp_connections(ip_addrs, num_nodes, node_rank);
}

} //namespace tcp

} //namespace sst
