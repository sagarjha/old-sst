#ifndef VERBS_H
#define VERBS_H

#include <infiniband/verbs.h>

namespace sst {

/* structure to exchange data which is needed to connect the QPs */
struct cm_con_data_t {
    // Buffer address
    uint64_t addr;
    // Remote key
    uint32_t rkey;
    // QP number
    uint32_t qp_num;
    // LID of the IB port
    uint16_t lid;
    // gid
    uint8_t gid[16];
}__attribute__((packed));

class resources {
    private:
        // initialize queue pair
        void modify_qp_to_init();
        // drive qp to ready-to-receive state
        void modify_qp_to_rtr();
        // drive qp to ready-to-send state
        void modify_qp_to_rts();
        // connect the queue pairs
        void connect_qp();
        // post remote operation
        int post_remote_send(long long int offset, long long int size, int op);

    public:
        // index of the corresponding node
        int remote_index;
        // QP handle
        struct ibv_qp *qp;
        // MR handle for buf
        struct ibv_mr *write_mr, *read_mr;
        // values to connect to remote side
        struct cm_con_data_t remote_props;
        // memory buffer pointer, used for RDMA remote reads and local writes
        char *write_buf, *read_buf;

        // constructor, initializes qp, mr and remote_props
        resources(int r_index, char* write_addr, char* read_addr, int size_w,
                int size_r);
        // destroy the resources
        virtual ~resources();
        /*
         wrapper functions that make up the user interface
         all call post_remote_send with different parameters
         */
        //post an RDMA read with start address of remote memory
        void post_remote_read(long long int size);
        // post an RDMA operation with an offset into remote memory
        void post_remote_read(long long int offset, long long int size);
        // corresponding functions for RDMA write
        void post_remote_write(long long int size);
        void post_remote_write(long long int offset, long long int size);
};

// initialize the global verbs resources
void verbs_initialize();
// poll for completion of the posted remote read
void verbs_poll_completion();
// destroy the global resources
void verbs_destroy();

} //namespace sst

#endif //VERBS_H
