#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <pthread.h>
#include "msocket.h"

// sembuf
struct sembuf pop, vop;
#define down(s) semop(s, &pop, 1) // wait(s)
#define up(s) semop(s, &vop, 1)   // signal(s)

// argument for threads
typedef struct argtype
{
    mtp_socket *MTP_Table;
    shared_variables *shared_resource;
} argtype;

// global varibales
volatile sig_atomic_t sigint_received = 0;
int sm_id_MTP_Table, sm_id_shared_vars;
int mtx_table_info;
int mtx_swnd, mtx_recvbuf, mtx_sendbuf;

int total_message_sent = 0;

/*
    Function: sock_converter
    Arguments: char *ip, unsigned short port
    Return Value: struct sockaddr_in
    Workflow: Converts a given IP address and port number into a sockaddr_in structure.
              It initializes a sockaddr_in structure, sets the IP address, family (IPv4), and port number,
              then returns the structure.
*/
struct sockaddr_in sock_converter(char *ip, unsigned short port)
{
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    return addr;
}

/*
    Function: max_logical
    Arguments: int x, int y, int size
    Return Value: int
    Workflow: Finds the maximum sequence number out of two in the send window considering a circular fashion.
              It first calculates the space between x and y, then compares it with half the size.
              If the space is less than half the size, it returns the maximum of x and y.
              Otherwise, it returns the minimum of x and y, considering the wrap-around.
*/
int max_logical(int x, int y, int size)
{
    int space = abs(x - y);
    if (space < size / 2)
    {
        return (x >= y) ? x : y;
    }
    else
    {
        return (x >= y) ? y : x;
    }
}

/*
    Function: min_logical
    Arguments: int x, int y, int size
    Return Value: int
    Workflow: Finds the minimum sequence number out of two in the send window considering a circular fashion.
              It first calculates the space between x and y, then compares it with half the size.
              If the space is less than half the size, it returns the minimum of x and y.
              Otherwise, it returns the maximum of x and y, considering the wrap-around.
*/
int min_logical(int x, int y, int size)
{
    int space = abs(x - y);
    if (space < size / 2)
    {
        return (x <= y) ? x : y;
    }
    else
    {
        return (x <= y) ? y : x;
    }
}

/*
    Function: my_strcpy
    Arguments: char *a1, char *a2, int size
    Return Value: void
    Workflow: Copies characters from string a2 to string a1 up to the specified size.
              It iterates through each character of a2 and copies it to the corresponding position in a1,
              up to the given size. If the size of a2 is less than the specified size,
              the remaining characters in a1 are filled with null terminators.
*/
void my_strcpy(char *a1, char *a2, int size)
{
    for (int i = 0; i < size; i++)
    {
        a1[i] = a2[i];
    }
    return;
}

/*---------------------------------------MAIN THREAD----------------------------------------------------------*/
/*
    Function: sigint_handler
    Arguments: int signum
    Return Value: void
    Workflow: Signal handler for SIGINT signal. Sets the flag sigint_received to 1 indicating the signal is received.
              It then cleans up shared memory segments using shmctl and exits the process.
*/
void sigint_handler(int signum)
{
    if (signum == SIGINT)
    {
        sigint_received = 1;

        shmctl(sm_id_shared_vars, 0, 0);
        shmctl(sm_id_MTP_Table, 0, 0);
        exit(0);
    }
    return;
}

/*
    Function: create_shared_MTP_Table
    Arguments: None
    Return Value: mtp_socket *
    Workflow: Creates a shared memory segment for the MTP table.
              It first generates a key using ftok function based on the current directory and KEY_MTP_TABLE.
              Then it obtains a shared memory identifier using shmget function with the generated key,
              allocating memory for SIZE_SM number of mtp_socket structures.
              Finally, it attaches the shared memory segment to the process address space using shmat
              and returns a pointer to the MTP table.
*/
mtp_socket *create_shared_MTP_Table()
{
    int sm_key = ftok(".", KEY_MTP_TABLE);
    int sm_id_MTP_Table = shmget(sm_key, (SIZE_SM) * sizeof(mtp_socket), 0777 | IPC_CREAT);
    mtp_socket *MTP_Table = (mtp_socket *)shmat(sm_id_MTP_Table, 0, 0);
    return MTP_Table;
}

/*
    Function: create_shared_variables
    Arguments: None
    Return Value: shared_variables *
    Workflow: Creates a shared memory segment for shared variables.
              It first generates a key using ftok function based on the current directory and KEY_SHARED_RESOURCE.
              Then it obtains a shared memory identifier using shmget function with the generated key,
              allocating memory for the size of shared_variables structure.
              Finally, it attaches the shared memory segment to the process address space using shmat
              and returns a pointer to the shared variables.
*/
shared_variables *create_shared_variables()
{
    int sm_key = ftok(".", KEY_SHARED_RESOURCE);
    int sm_id_shared_vars = shmget(sm_key, sizeof(int), 0777 | IPC_CREAT);
    shared_variables *vars = (shared_variables *)shmat(sm_id_shared_vars, 0, 0);
    return vars;
}

/*
    Function: create_entry_semaphore
    Arguments: int *id
    Return Value: void
    Workflow: Creates a semaphore for entry synchronization.
              It first generates a key using ftok function based on the current directory and KEY_ENTRY_SEM.
              Then it obtains a semaphore identifier using semget function with the generated key,
              creating a new semaphore if it does not exist, and setting the permission to 0777 | IPC_CREAT.
              Afterward, it initializes the semaphore value to 0 using semctl.
*/
void create_entry_semaphore(int *id)
{
    int sm_key = ftok(".", KEY_ENTRY_SEM);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
    semctl(*id, 0, SETVAL, 0);
}

/*
    Function: create_exit_semaphore
    Arguments: int *id
    Return Value: void
    Workflow: Creates a semaphore for exit synchronization.
              It first generates a key using ftok function based on the current directory and KEY_EXIT_SEM.
              Then it obtains a semaphore identifier using semget function with the generated key,
              creating a new semaphore if it does not exist, and setting the permission to 0777 | IPC_CREAT.
              Afterward, it initializes the semaphore value to 0 using semctl.
*/
void create_exit_semaphore(int *id)
{
    int sm_key = ftok(".", KEY_EXIT_SEM);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
    semctl(*id, 0, SETVAL, 0);
}

/*
    Function: create_mtx_table_info
    Arguments: int *id
    Return Value: void
    Workflow: Creates a mutex semaphore for controlling access to the MTP table information.
              It first generates a key using ftok function based on the current directory and KEY_MUTEX.
              Then it obtains a semaphore identifier using semget function with the generated key,
              creating a new semaphore if it does not exist, and setting the permission to 0777 | IPC_CREAT.
              Afterward, it initializes the semaphore value to 1 to indicate the mutex is available using semctl.
*/
void create_mtx_table_info(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
    semctl(*id, 0, SETVAL, 1);
}

/*
    Function: create_mutex_swnd
    Arguments: int *id
    Return Value: void
    Workflow: Creates a mutex semaphore for controlling access to the send window.
              It first generates a key using ftok function based on the current directory and KEY_MUTEX_SWND.
              Then it obtains a semaphore identifier using semget function with the generated key,
              creating a new semaphore if it does not exist, and setting the permission to 0777 | IPC_CREAT.
              Afterward, it initializes the semaphore value to 1 to indicate the mutex is available using semctl.
*/
void create_mutex_swnd(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX_SWND);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
    semctl(*id, 0, SETVAL, 1);
}

/*
    Function: create_mutex_recvbuf
    Arguments: int *id
    Return Value: void
    Workflow: Creates a mutex semaphore for controlling access to the receive buffer.
              It first generates a key using ftok function based on the current directory and KEY_MUTEX_RECVBUF.
              Then it obtains a semaphore identifier using semget function with the generated key,
              creating a new semaphore if it does not exist, and setting the permission to 0777 | IPC_CREAT.
              Afterward, it initializes the semaphore value to 1 to indicate the mutex is available using semctl.
*/
void create_mutex_recvbuf(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX_RECVBUF);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
    semctl(*id, 0, SETVAL, 1);
}

/*
    Function: create_mutex_sendbuf
    Arguments: int *id
    Return Value: void
    Workflow: Creates a mutex semaphore for controlling access to the send buffer.
              It first generates a key using ftok function based on the current directory and KEY_MUTEX_SENDBUF.
              Then it obtains a semaphore identifier using semget function with the generated key,
              creating a new semaphore if it does not exist, and setting the permission to 0777 | IPC_CREAT.
              Afterward, it initializes the semaphore value to 1 to indicate the mutex is available using semctl.
*/
void create_mutex_sendbuf(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX_SENDBUF);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
    semctl(*id, 0, SETVAL, 1);
}

/*
    Function: dropMessage
    Arguments: float p
    Return Value: int
    Workflow: Simulates dropping a message with a given probability. It generates a random number between 0 and 1,
              compares it with the given probability p. If the random number is less than p, it returns 1 indicating
              success (message dropped). Otherwise, it returns 0 indicating failure (message not dropped).
*/
int dropMessage(float p)
{
    float random_number = (float)(rand() % 1000) / 1000;

    if (random_number < p)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/*
    Function: socket_handler
    Arguments: mtp_socket *MTP_Table, shared_variables *shared_resource, int *entry_sem, int *exit_sem
    Return Value: void
    Workflow: Handles socket operations such as creation, binding, and closing in response to requests from user processes.
              It continuously waits for requests by blocking on the entry semaphore.
              Upon receiving a request, it performs the appropriate socket operation based on the shared_resource status.
              It updates the return value and error number in the shared resource accordingly.
              After processing, it signals the user process by releasing the exit semaphore.
*/
void socket_handler(mtp_socket *MTP_Table, shared_variables *shared_resource, int *entry_sem, int *exit_sem)
{
    int socket_id;
    /* The main thread(after creating R and S) for the rest of its lifetime
    server the user processes for creating, binding and closing the sockets */

    printf("Main Thread ready to go...\n");
    while (1)
    {
        down(*entry_sem);
        shared_resource->error_no = 0;
        /* Respond to socket creation call */
        if (shared_resource->status == 0)
        {
            socket_id = socket(AF_INET, SOCK_DGRAM, 0);
            MTP_Table[shared_resource->mtp_id].udp_sockid = socket_id;
            shared_resource->return_value = socket_id;
            shared_resource->error_no = errno;
        }
        /* Respond to bind call */
        else if (shared_resource->status == 1)
        {
            socket_id = MTP_Table[shared_resource->mtp_id].udp_sockid;
            shared_resource->return_value = bind(socket_id, (const struct sockaddr *)&(shared_resource->src_addr), sizeof(shared_resource->src_addr));
            shared_resource->error_no = errno;
        }
        /* Respond to close call */
        else if (shared_resource->status == 2)
        {
            socket_id = MTP_Table[shared_resource->mtp_id].udp_sockid;
            shared_resource->return_value = close(socket_id);
            shared_resource->error_no = errno;
        }
        shared_resource->status = -1;
        up(*exit_sem);
    }
}

/*--------------------------------------------------------------------------------------------------------------------*/

/*----------------------------------------------------R THREAD----------------------------------------------------------*/

/*
    Function: R_Thread
    Arguments:
        void *arg: A void pointer to the argument passed to the thread function.
    Return Value: None (void *)

    Workflow:
        - Extract the total shared resources from the argument.
        - Initialize local pointers to the MTP socket table and shared variables.
        - Acquire mutex lock for the receive buffer.
        - Initialize the receive buffer and receive window variables for each socket ID.
        - Release the mutex lock.
        - Prepare for using the select system call.
        - Enter an infinite loop for continuous operation.
        - Set up file descriptors for select with a timeout.
        - Monitor sockets for incoming data or timeout.
        - Process incoming messages or timeout accordingly.
        - If a message is received, handle acknowledgment or user data accordingly.
        - Update receive window and send acknowledgment.
        - If the receive buffer was earlier acknowledged to be full, update the receive window and resend acknowledgment.
*/
void *R_Thread(void *arg)
{
    argtype *total_shared_resource = (argtype *)arg;
    mtp_socket *MTP_Table = total_shared_resource->MTP_Table;
    shared_variables *shared_resource = total_shared_resource->shared_resource;

    // initialize all varibles of receive window and receive buffer
    down(mtx_recvbuf);
    for (int i = 0; i < SIZE_SM; i++)
    {
        for (int k = 0; k < RECV_BUFFSIZE; k++)
        {
            MTP_Table[i].recv_buff[k].sequence_no = -1;
        }
        for (int k = 0; k < RWND_SIZE; k++)
        {
            MTP_Table[i].rwnd.window[k] = k + 1;
        }
        MTP_Table[i].rwnd.nospace = 0;
        MTP_Table[i].rwnd.last_inorder_received = 0;
        MTP_Table[i].rwnd.last_user_taken = 0;
    }
    up(mtx_recvbuf);

    fd_set read_fds;
    int max_fd_value = 0;
    int action = 0;
    struct timeval tout;

    printf("R Thread ready to go...\n");

    while (1)
    {
        tout.tv_sec = TIMEOUT_S;
        tout.tv_usec = TIMEOUT_US;

        FD_ZERO(&read_fds);
        for (int i = 0; i < SIZE_SM; i++)
        {
            if (!MTP_Table[i].free)
            {
                FD_SET(MTP_Table[i].udp_sockid, &read_fds);

                if (MTP_Table[i].udp_sockid > max_fd_value)
                    max_fd_value = MTP_Table[i].udp_sockid;
            }
        }

        // wait on select call
        action = select(max_fd_value + 1, &read_fds, NULL, NULL, &tout);

        /*  Timeout  */
        if (action == 0)
        {
            // continue; //// DO NOT USE IT
        }

        for (int i = 0; i < SIZE_SM; i++)
        {
            if (!MTP_Table[i].free)
            {

                /* Some message received */
                if (FD_ISSET(MTP_Table[i].udp_sockid, &read_fds))
                {
                    char udp_data[KB + 6];
                    int udp_id = MTP_Table[i].udp_sockid;
                    struct sockaddr_in dest_addr = sock_converter(MTP_Table[i].dest_ip, MTP_Table[i].dest_port);
                    int len = sizeof(dest_addr);
                    int bytes = recvfrom(udp_id, udp_data, KB + 6, 0, (struct sockaddr *)&dest_addr, &len);

                    // if error
                    if (bytes <= 0)
                        continue;

                    // Drop the message with probability P
                    if (dropMessage((float)P))
                    {
                        continue;
                    }

                    udp_data[bytes] = '\0';

                    // Message is acknowledgement
                    if (udp_data[0] == 'A')
                    {
                        down(mtx_swnd);
                        int ack_seqno = (int)(udp_data[1] - 'a');
                        int curr_empty_space = (int)(udp_data[2] - 'a');
                        send_window curr_swnd = MTP_Table[i].swnd;
                        int last_ack_seqno = curr_swnd.last_ack_seqno;

                        int flag_dup_seq_ack = 0;
                        if (max_logical(last_ack_seqno, ack_seqno, MAX_SEQ_NO) == last_ack_seqno && curr_swnd.last_ack_emptyspace == curr_empty_space)
                        {
                            // Duplicate ACK received
                            up(mtx_swnd);
                            continue;
                        }
                        else
                        {
                            if (max_logical(last_ack_seqno, ack_seqno, MAX_SEQ_NO) == last_ack_seqno && curr_swnd.last_ack_emptyspace != curr_empty_space)
                            {
                                flag_dup_seq_ack = 1;
                            }

                            // Update the send window upon receiving valid ACK
                            down(mtx_sendbuf);

                            int k = MTP_Table[i].swnd.left_idx;
                            if (flag_dup_seq_ack == 0)
                            {
                                while (MTP_Table[i].send_buff[k].sequence_no != ack_seqno)
                                {
                                    MTP_Table[i].send_buff[k].sequence_no = -1;
                                    k = (k + 1) % SEND_BUFFSIZE;
                                }
                                MTP_Table[i].send_buff[k].sequence_no = -1;
                                k = (k + 1) % SEND_BUFFSIZE;
                            }

                            MTP_Table[i].swnd.left_idx = k;
                            MTP_Table[i].swnd.right_idx = (k + curr_empty_space - 1 + SEND_BUFFSIZE) % SEND_BUFFSIZE;
                            MTP_Table[i].swnd.last_ack_seqno = ack_seqno;
                            MTP_Table[i].swnd.last_ack_emptyspace = curr_empty_space;

                            up(mtx_sendbuf);
                        }

                        up(mtx_swnd);
                    }
                    // Message is user data
                    else
                    {
                        down(mtx_recvbuf);

                        // Insert the user data in the receive buffer at appropriate position
                        int new_data_received = 0;
                        int seq_no = (int)(udp_data[1] - 'a');
                        for (int k = 0; k < RWND_SIZE; k++)
                        {
                            if (MTP_Table[i].rwnd.window[k] == seq_no)
                            {
                                for (int e = 0; e < RECV_BUFFSIZE; e++)
                                {
                                    if (MTP_Table[i].recv_buff[e].sequence_no == -1)
                                    {
                                        // put data in receive buffer
                                        new_data_received = 1;
                                        MTP_Table[i].recv_buff[e].sequence_no = seq_no;
                                        my_strcpy(MTP_Table[i].recv_buff[e].data, udp_data + 2, KB);

                                        break;
                                    }
                                }
                                break;
                            }
                        }

                        // Calculate the empty space and reconstruct the receive window
                        int empty_space = 0;
                        int seqno_at_buff[MAX_SEQ_NO + 1];
                        memset(seqno_at_buff, 0, sizeof(seqno_at_buff));

                        for (int k = 0; k < RECV_BUFFSIZE; k++)
                        {
                            if (MTP_Table[i].recv_buff[k].sequence_no == -1)
                            {
                                empty_space++;
                            }
                            else
                            {
                                seqno_at_buff[MTP_Table[i].recv_buff[k].sequence_no] = 1;
                            }
                        }

                        int curr = (MTP_Table[i].rwnd.last_inorder_received) % MAX_SEQ_NO + 1;
                        while (1)
                        {
                            if (seqno_at_buff[curr] == 1)
                            {
                                MTP_Table[i].rwnd.last_inorder_received = curr;
                            }
                            else
                            {
                                break;
                            }
                            curr = (curr) % MAX_SEQ_NO + 1;
                        }

                        if (empty_space != 0)
                        {
                            // Update receive window
                            memset(MTP_Table[i].rwnd.window, -1, sizeof(MTP_Table[i].rwnd.window));
                            int rwnd_idx = 0;
                            int curr = (MTP_Table[i].rwnd.last_inorder_received) % MAX_SEQ_NO + 1;

                            for (int e = 1; e <= empty_space; e++)
                            {
                                while (seqno_at_buff[curr] != 0)
                                {
                                    curr = (curr) % MAX_SEQ_NO + 1;
                                }
                                MTP_Table[i].rwnd.window[rwnd_idx++] = curr;
                                curr = (curr) % MAX_SEQ_NO + 1;
                            }
                            for (; rwnd_idx < RWND_SIZE;)
                            {
                                MTP_Table[i].rwnd.window[rwnd_idx++] = -1;
                            }
                            //*******************************
                            // First message received after sending special ACK for having space after nospace flag has been set
                            if (MTP_Table[i].rwnd.nospace == 1 && new_data_received)
                            {
                                MTP_Table[i].rwnd.nospace = 0;
                            }
                            //*******************************
                        }

                        // Send the ACK
                        char ACK_data_udp[3] = {'A', (char)(MTP_Table[i].rwnd.last_inorder_received + 'a'), (char)(empty_space + 'a')};
                        int udp_id = MTP_Table[i].udp_sockid;
                        struct sockaddr_in dest_addr = sock_converter(MTP_Table[i].dest_ip, MTP_Table[i].dest_port);
                        int bytes = sendto(udp_id, ACK_data_udp, 3, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                        if (empty_space == 0)
                        {
                            MTP_Table[i].rwnd.nospace = 1;
                        }
                        up(mtx_recvbuf);
                    }
                }
                else
                {
                    /* Receive buffer was acknowledged to be full earlier */
                    if (MTP_Table[i].rwnd.nospace == 1)
                    {
                        down(mtx_recvbuf);

                        // Calculate the empty space and reconstruct the receive window
                        int empty_space = 0;
                        int seqno_at_buff[MAX_SEQ_NO + 1];
                        memset(seqno_at_buff, 0, sizeof(seqno_at_buff));

                        for (int k = 0; k < RECV_BUFFSIZE; k++)
                        {
                            if (MTP_Table[i].recv_buff[k].sequence_no == -1)
                            {
                                empty_space++;
                            }
                            else
                            {
                                seqno_at_buff[MTP_Table[i].recv_buff[k].sequence_no] = 1;
                            }
                        }
                        //////////////////////////////////////////////////////////////////////////////////////////////////////////
                        if (empty_space != 0)
                        {
                            // Empty space in receive buffer

                            // Update receive window
                            memset(MTP_Table[i].rwnd.window, -1, sizeof(MTP_Table[i].rwnd.window));
                            int rwnd_idx = 0;
                            int curr = (MTP_Table[i].rwnd.last_inorder_received) % MAX_SEQ_NO + 1;
                            for (int e = 1; e <= empty_space; e++)
                            {
                                while (seqno_at_buff[curr] != 0)
                                {
                                    curr = (curr) % MAX_SEQ_NO + 1;
                                }
                                MTP_Table[i].rwnd.window[rwnd_idx++] = curr;
                                curr = (curr) % MAX_SEQ_NO + 1;
                            }
                            for (; rwnd_idx < RWND_SIZE;)
                            {
                                MTP_Table[i].rwnd.window[rwnd_idx++] = -1;
                            }

                            // Send the ACK
                            char ACK_data_udp[3] = {'A', (char)(MTP_Table[i].rwnd.last_inorder_received + 'a'), (char)(empty_space + 'a')};
                            int udp_id = MTP_Table[i].udp_sockid;
                            struct sockaddr_in dest_addr = sock_converter(MTP_Table[i].dest_ip, MTP_Table[i].dest_port);
                            int bytes = sendto(udp_id, ACK_data_udp, 3, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                        }
                        up(mtx_recvbuf);
                    }
                }
            }
        }
    }
    pthread_exit(NULL);
}

/*--------------------------------------------------------------------------------------------------------------------*/

/*----------------------------------------------------S THREAD----------------------------------------------------------*/

/*
    Function: S_Thread
    Arguments:
        void *arg: A void pointer to the argument passed to the thread function.
    Return Value: None (void *)

    Workflow:
        - Extract the total shared resources from the argument.
        - Initialize local pointers to the MTP socket table and shared variables.
        - Acquire mutex locks for the send window and send buffer.
        - Initialize the send window and send buffer variables for each socket ID.
        - Release the mutex locks.
        - Enter an infinite loop for continuous operation.
        - Iterate over each socket ID in the MTP socket table.
        - If the socket is not free, acquire mutex locks for the send window and send buffer.
        - Check the valid portion of the send window for each socket ID.
        - Determine if there is a timeout condition for unacknowledged messages.
        - Resend unacknowledged messages if a timeout occurred.
        - Otherwise, send the next available messages.
        - Release the mutex locks.
        - Sleep for half the timeout period before the next iteration.
*/
void *S_Thread(void *arg)
{
    argtype *total_shared_resource = (argtype *)arg;
    mtp_socket *MTP_Table = total_shared_resource->MTP_Table;
    shared_variables *shared_resource = total_shared_resource->shared_resource;

    down(mtx_swnd);
    down(mtx_sendbuf);

    // initialize all varibles of send window and send buffer
    for (int i = 0; i < SIZE_SM; i++)
    {
        for (int k = 0; k < SEND_BUFFSIZE; k++)
        {
            MTP_Table[i].send_buff[k].sequence_no = -1;
        }
        MTP_Table[i].swnd.left_idx = 0;
        MTP_Table[i].swnd.right_idx = (RECV_BUFFSIZE - 1) % SEND_BUFFSIZE;
        MTP_Table[i].swnd.new_entry = 0;
        MTP_Table[i].swnd.last_seq_no = 0;
        MTP_Table[i].swnd.last_sent = -1;
        MTP_Table[i].swnd.last_ack_seqno = 0;
        for (int k = 0; k < SWND_SIZE; k++)
        {
            MTP_Table[i].swnd.last_active_time[k] = 0;
        }
    }
    up(mtx_sendbuf);
    up(mtx_swnd);

    printf("S Thread ready to go...\n");

    while (1)
    {
        for (int i = 0; i < SIZE_SM; i++)
        {
            if (!MTP_Table[i].free)
            {
                down(mtx_swnd);
                down(mtx_sendbuf);
                // Checking valid portion to send
                send_window curr_swnd = MTP_Table[i].swnd;

                int left = curr_swnd.left_idx;
                int right = curr_swnd.right_idx;

                int is_tout = 0;
                if ((right + 1) % SEND_BUFFSIZE == left)
                {
                    // SWND EMPTY
                    up(mtx_sendbuf);
                    up(mtx_swnd);

                    continue;
                }

                time_t curr_time = time(NULL);

                while (left != (right + 1) % SEND_BUFFSIZE)
                {
                    if ((curr_time - curr_swnd.last_active_time[left] > T) && (curr_swnd.last_active_time[left] > 0) && (MTP_Table[i].send_buff[left].sequence_no > 0))
                    {
                        is_tout = 1;
                        break;
                    }
                    left = (left + 1) % SEND_BUFFSIZE;
                }

                // timeout occurred for some message
                if (is_tout)
                {
                    left = curr_swnd.left_idx;
                    right = curr_swnd.right_idx;

                    while (left != (right + 1) % SEND_BUFFSIZE)
                    {
                        if (MTP_Table[i].send_buff[left].sequence_no < 0)
                        {
                            break;
                        }
                        char udp_data[KB + 2];
                        udp_data[0] = 'D';
                        udp_data[1] = (char)(MTP_Table[i].send_buff[left].sequence_no + 'a');
                        my_strcpy(udp_data + 2, MTP_Table[i].send_buff[left].data, KB);
                        struct sockaddr_in dest_addr = sock_converter(MTP_Table[i].dest_ip, MTP_Table[i].dest_port);
                        int bytes = sendto(MTP_Table[i].udp_sockid, udp_data, KB + 2, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                        total_message_sent++;
                        printf("Total message sent : %d\n", total_message_sent);

                        MTP_Table[i].swnd.last_active_time[left] = time(NULL);

                        MTP_Table[i].swnd.last_sent = left;

                        left = (left + 1) % SEND_BUFFSIZE;
                    }
                }
                else
                {
                    left = (curr_swnd.last_sent + 1) % SEND_BUFFSIZE;
                    right = curr_swnd.right_idx;
                    int is_data_unsend = 1;

                    if (max_logical(left, right, SEND_BUFFSIZE) == left && left != right)
                    {
                        is_data_unsend = 0;
                    }

                    while (left != (right + 1) % SEND_BUFFSIZE && is_data_unsend)
                    {
                        if (MTP_Table[i].send_buff[left].sequence_no < 0)
                        {
                            break;
                        }
                        char udp_data[KB + 2];
                        udp_data[0] = 'D';
                        udp_data[1] = (char)(MTP_Table[i].send_buff[left].sequence_no + 'a');
                        my_strcpy(udp_data + 2, MTP_Table[i].send_buff[left].data, KB);
                        struct sockaddr_in dest_addr = sock_converter(MTP_Table[i].dest_ip, MTP_Table[i].dest_port);
                        int bytes = sendto(MTP_Table[i].udp_sockid, udp_data, KB + 2, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                        total_message_sent++;
                        printf("Total message sent : %d\n", total_message_sent);

                        MTP_Table[i].swnd.last_active_time[left] = time(NULL);

                        MTP_Table[i].swnd.last_sent = left;

                        left = (left + 1) % SEND_BUFFSIZE;
                    }
                }
                up(mtx_sendbuf);
                up(mtx_swnd);
            }
        }
        // sleep time for S thread
        sleep(T / 2);
    }
}

/*--------------------------------------------------------------------------------------------------------------------*/

/*----------------------------------------------------G THREAD----------------------------------------------------------*/

/*
    Function: G_Thread
    Arguments:
        void *arg: A void pointer to the argument passed to the thread function.
    Return Value: None (void *)

    Brief Workflow:
        - Extract the total shared resources from the argument.
        - Initialize local pointers to the MTP socket table and shared variables.
        - Enter an infinite loop for continuous operation.
        - Iterate over each socket ID in the MTP socket table.
        - Check if the associated process exists using the kill system call.
        - If the process exists, continue; otherwise, perform cleanup operations:
            - Acquire mutex locks for the send and receive buffers.
            - Reset send window and send buffer variables.
            - Reset receive window and receive buffer variables.
            - Set the socket as free and close its associated UDP socket.
            - Release the mutex locks.
        - Sleep for the specified garbage collection time.
*/
void *G_Thread(void *arg)
{

    argtype *total_shared_resource = (argtype *)arg;
    mtp_socket *MTP_Table = total_shared_resource->MTP_Table;
    shared_variables *shared_resource = total_shared_resource->shared_resource;

    printf("G Thread ready to go...\n");

    while (1)
    {
        down(mtx_table_info);
        for (int i = 0; i < SIZE_SM; i++)
        {
            if (MTP_Table[i].free == 0)
            {
                if (kill(MTP_Table[i].pid, 0) == 0)
                {
                    // process exists
                }
                else
                {
                    if (errno == ESRCH)
                    {
                        // process does not exist

                        down(mtx_swnd);
                        down(mtx_sendbuf);
                        for (int k = 0; k < SEND_BUFFSIZE; k++)
                        {
                            MTP_Table[i].send_buff[k].sequence_no = -1;
                        }
                        MTP_Table[i].swnd.left_idx = 0;
                        MTP_Table[i].swnd.right_idx = (RECV_BUFFSIZE - 1) % SEND_BUFFSIZE;
                        MTP_Table[i].swnd.new_entry = 0;
                        MTP_Table[i].swnd.last_seq_no = 0;
                        MTP_Table[i].swnd.last_sent = -1;
                        MTP_Table[i].swnd.last_ack_seqno = 0;
                        for (int k = 0; k < SWND_SIZE; k++)
                        {
                            MTP_Table[i].swnd.last_active_time[k] = 0;
                        }
                        up(mtx_sendbuf);
                        up(mtx_swnd);

                        down(mtx_recvbuf);
                        for (int k = 0; k < RECV_BUFFSIZE; k++)
                        {
                            MTP_Table[i].recv_buff[k].sequence_no = -1;
                        }
                        for (int k = 0; k < RWND_SIZE; k++)
                        {
                            MTP_Table[i].rwnd.window[k] = k + 1;
                        }
                        MTP_Table[i].rwnd.nospace = 0;
                        MTP_Table[i].rwnd.last_inorder_received = 0;
                        MTP_Table[i].rwnd.last_user_taken = 0;
                        up(mtx_recvbuf);

                        MTP_Table[i].free = 1;
                        close(MTP_Table[i].udp_sockid);
                    }
                    else
                    {
                        up(mtx_table_info);
                        perror("kill");
                        pthread_exit(NULL);
                    }
                }
            }
        }
        up(mtx_table_info);
        sleep(GARBAGE_T);
    }
}
/*--------------------------------------------------------------------------------------------------------------------*/

/*
    Function: main
    Arguments: None
    Return Value: Integer indicating the exit status of the program.

    Brief Workflow:
        - Register signal handler for SIGINT.
        - Seed the random number generator.
        - Initialize sembuf structures for P(s) and V(s) operations.
        - Create entry semaphore for synchronization.
        - Create exit semaphore for synchronization.
        - Create mutexes for thread synchronization.
        - Create shared memory for the MTP socket table.
        - Initialize the MTP socket table with default values.
        - Create shared resources for communication with user processes.
        - Create threads for R, S, and G operations.
        - Sleep briefly for thread initialization.
        - Handle socket operations for communication.
        - Join R, S, and G threads upon completion.
*/
int main()
{

    signal(SIGINT, sigint_handler);
    srand(time(NULL));

    // populating sembuf appropiately for P(s) and V(s)
    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1;
    vop.sem_op = 1;

    /* Entry Semaphore creation */
    int entry_sem;
    create_entry_semaphore(&entry_sem);

    /* Exit Semaphore creation */
    int exit_sem;
    create_exit_semaphore(&exit_sem);

    create_mtx_table_info(&mtx_table_info);

    create_mutex_swnd(&mtx_swnd);

    create_mutex_recvbuf(&mtx_recvbuf);

    create_mutex_sendbuf(&mtx_sendbuf);

    /* Shared Memory creation */
    mtp_socket *MTP_Table = create_shared_MTP_Table();
    for (int i = 0; i < SIZE_SM; i++)
    {
        MTP_Table[i].free = 1;
        MTP_Table[i].pid = i + 5;
        // MTP_Table[i].udp_sockid = 256;
    }

    /* Shared Resouces creation for communication with the user process */
    shared_variables *shared_resource = create_shared_variables();

    pthread_t R, S, G;

    argtype *arg = (argtype *)malloc(sizeof(argtype));
    arg->MTP_Table = MTP_Table;
    arg->shared_resource = shared_resource;

    // Create R thread
    if (pthread_create(&R, NULL, R_Thread, (void *)arg) != 0)
    {
        perror("Failed to create R thread");
        exit(EXIT_FAILURE);
    }

    // Create S thread
    if (pthread_create(&S, NULL, S_Thread, (void *)arg) != 0)
    {
        perror("Failed to create S thread");
        exit(EXIT_FAILURE);
    }

    // Create G thread
    if (pthread_create(&G, NULL, G_Thread, (void *)arg) != 0)
    {
        perror("Failed to create S thread");
        exit(EXIT_FAILURE);
    }

    usleep(100);

    socket_handler(MTP_Table, shared_resource, &entry_sem, &exit_sem);

    // Join R thread
    if (pthread_join(R, NULL) != 0)
    {
        perror("Failed to join R thread");
        exit(EXIT_FAILURE);
    }

    // Join S thread
    if (pthread_join(S, NULL) != 0)
    {
        perror("Failed to join S thread");
        exit(EXIT_FAILURE);
    }

    // Join G thread
    if (pthread_join(G, NULL) != 0)
    {
        perror("Failed to join G thread");
        exit(EXIT_FAILURE);
    }
}
