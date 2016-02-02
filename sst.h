#ifndef SST_H
#define SST_H

#include <thread>
#include <stdlib.h>
#include <string.h>
#include <cassert>
#include <functional>
#include <list>
#include <type_traits>
#include <utility>
#include <vector>

#include "predicates.h"
#include "verbs.h"

namespace sst {

  using std::vector;
  using std::function;
  using std::list;
  using std::pair;
  using std::thread;

  template <class Row>
  class SST_reads;
  template <class Row>
  class SST_writes;

  // common SST object
  template <class Row>
  class SST : public Row {
    // struct must be a pod. In addition, it should not contain any pointer types
    static_assert(std::is_pod<Row>::value, "Error!");

  protected:
    // list of members in the group
    vector <int> members;
    // number of members
    int num_members;
    // index of current node in the group
    int member_index;
    // the key value storage structure
    vector <Row> table;
  
  public:
    // constructor for the state table
    SST (const vector <int> &_members, int _node_rank);
    // get (local or remote) function
    const Row & get (int index);
    // [] operator for the SST
    const Row & operator [] (int index);
    // returns the number of rows
    int get_num_rows ();
    // returns the member_index
    int get_mem_index ();
    // snapshot of the table
    const vector <Row> get_snapshot ();
  };

  // SST-reads version
  template <class Row>
  class SST_reads : public SST<Row> {
    using SST<Row>::members;
    using SST<Row>::num_members;
    using SST<Row>::member_index;
    using SST<Row>::table;
  
    // resources vector, one for each member
    vector <resources *> res_vec;

  public:
    // takes care of the predicates
    Predicates_read<Row> predicates;

    // constructor for SST-reads
    SST_reads (const vector <int> &_members, int _node_rank);
    // reads all the remote rows by RDMA
    void refresh_table ();
    // refreshes continously
    void read ();
    // detect predicates continuously
    void detect ();
  };

  template <class Row>
  class SST_writes : public SST<Row> {
    using SST<Row>::members;
    using SST<Row>::num_members;
    using SST<Row>::member_index;
    using SST<Row>::table;
  
    // resources vector, one for each member
    vector <resources *> res_vec;
  
  public:
    // takes care of the predicates
    Predicates_write<Row> predicates;

    // constructor for the SST-writes
    SST_writes (const vector <int> &_members, int _node_rank);
    // write local row to all remote nodes
    void put ();
    // write a contiguous area of row (in particular, a single state variable ) to all remote nodes
    void put(long long int offset, long long int size);
    // detect predicates continuously
    void detect ();
  };

  // initialize SST object
  template <class Row>
  SST<Row>::SST (const vector <int> & _members, int _node_rank) {
    num_members = _members.size();
    members.resize (num_members);

    // copy members and figure out the member_index
    for (int i = 0; i < num_members; ++i) {
      members[i] = _members[i];
      if (members[i] == _node_rank) {
	member_index = i;
      }
    }

    // create the table
    table.resize (num_members);
  }

  // read a row (local or remote)
  template <class Row>
  const Row & SST<Row>::get (int index) {
    // check that the index is within range
    assert (index >= 0 && index < num_members);

    // check if get wants the local node's value
    if (member_index == index) {
      // return the object itself as it derives from Row
      return *this;
    }

    // return the table entry
    return table[index];
  }
  
  // [] operator overloaded for the get function
  template <class Row>
  const Row & SST<Row>::operator [] (int index){
    return get (index);
  }

  // returns the number of rows
  template <class Row>
  int SST<Row>::get_num_rows () {
    return num_members;
  }
  
  // returns the index of the local row in the table
  template <class Row>
  int SST<Row>::get_mem_index () {
    return member_index;
  }

  // returns a copy of the table for predicate evaluation
  template <class Row>
  const vector <Row> SST<Row>::get_snapshot () {
    // deep copy
    vector <Row> ret = table;
    // set own row
    ret[member_index] = *this;
    return ret;
  }

  // initializes the SST-read object
  template <class Row>
  SST_reads<Row>::SST_reads (const vector <int> & _members, int _node_rank) : SST<Row> (_members, _node_rank) {
    // resize the resources vector to number of members
    res_vec.resize (num_members);
  
    // initialize each element of res_vec
    for (int i = 0; i < num_members; ++i) {
      if (i != member_index) {
	// exchange lkey and addr of the table via tcp for enabling rdma reads
	res_vec[i] = new resources (members[i], (char *) this, (char *) &(table[i]), sizeof(table[0]), sizeof(table[0]));
      }
    }

    // create the reader and the detector thread
    thread reader (&SST_reads::read, this);
    thread detector (&SST_reads::detect, this);
    reader.detach();
    detector.detach();
  }

  // reads all the remote rows
  template <class Row>
  void SST_reads<Row>::refresh_table () {
    for (int index = 0; index < num_members; ++index) {
      if (index == member_index) {
	// don't read own row!
	continue;
      }
      // perform a remote RDMA read on the owner of the row
      res_vec[index]->post_remote_read (sizeof(table[0]));
    }
    // poll for one less than number of rows
    for (int index = 0; index < num_members-1; ++index) {
      // poll for completion
      poll_completion();
    }
  }

  // continuously refresh the table
  template <class Row>
  void SST_reads<Row>::read () {
    while (true) {
      refresh_table();
    }
  }

  // evaluate predicates one by one continuously and run the triggers
  template <class Row>
  void SST_reads<Row>::detect () {
    while (true) {
      // one time predicates need to be evaluated only until they become true
      auto it = predicates.one_time_predicates.begin();
      while (it != predicates.one_time_predicates.end()) {
	if (it->first (this) == true) {
	  for (auto func: it->second) {
	    func (this);
	  }
	  // erase the predicate as it was just found to be true
	  it = predicates.one_time_predicates.erase(it);
	}
	else {
	  it++;
	}
      }

      // recurrent predicates are evaluated each time they are found to be true
      it = predicates.recurrent_predicates.begin();
      while (it != predicates.recurrent_predicates.end()) {
	if (it->first (this) == true) {
	  for (auto func: it->second) {
	    func (this);
	  }
	}
	it++;
      }
    // take care of the transition predicates
    }
  }

  // initializes the SST_writes object
  template <class Row>
  SST_writes<Row>::SST_writes (const vector <int> & _members, int _node_rank) : SST<Row> (_members, _node_rank) {
    // resize the resources vector to number of members
    res_vec.resize (num_members);
  
    // initialize each element of res_vec
    for (int i = 0; i < num_members; ++i) {
      if (i != member_index) {
	// exchange lkey and addr of the table via tcp for enabling rdma writes
	res_vec[i] = new resources (members[i], (char *) &(table[i]), (char *) this, sizeof(table[0]), sizeof(table[0]));
      }
    }

    thread detector (&SST_writes::detect, this);
    detector.detach();
  }

  // write entire row to all the remote nodes
  template <class Row>
  void SST_writes<Row>::put () {
    for (int index = 0; index < num_members; ++index) {
      if (index == member_index) {
	// don't write to yourself!
	continue;
      }
      // perform a remote RDMA write on the owner of the row
      res_vec[index]->post_remote_write (sizeof(table[0]));
    }
    // poll for one less than number of rows
    for (int index = 0; index < num_members-1; ++index) {
      // poll for completion
      poll_completion();
    }
  }

  // write only a contiguous section of the row to all the remote nodes
  template <class Row>
  void SST_writes<Row>::put (long long int offset, long long int size) {
    for (int index = 0; index < num_members; ++index) {
      if (index == member_index) {
	// don't write to yourself!
	continue;
      }
      // perform a remote RDMA write on the owner of the row
      res_vec[index]->post_remote_write (offset, size);
    }
    // poll for one less than number of rows
    for (int index = 0; index < num_members-1; ++index) {
      // poll for completion
      poll_completion();
    }
  }

  // predicate detection for SST-writes
  template <class Row>
  void SST_writes<Row>::detect () {
    while (true) {
      // one time predicates need to be evaluated only until they become true
      auto it = predicates.one_time_predicates.begin();
      while (it != predicates.one_time_predicates.end()) {
	if (it->first (this) == true) {
	  for (auto func: it->second) {
	    func (this);
	  }
	  it = predicates.one_time_predicates.erase(it);
	}
	else {
	  it++;
	}
      }

      // recurrent predicates are evaluated each time they are found to be true
      it = predicates.recurrent_predicates.begin();
      while (it != predicates.recurrent_predicates.end()) {
	if (it->first (this) == true) {
	  for (auto func: it->second) {
	    func (this);
	  }
	}
	it++;
      }

      // take care of transition predicates
    }
  }

} /* namespace sst */

#endif /* SST_WRITE_H */
