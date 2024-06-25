#include "msocket.h"
#define ERR -1
#define SUCC 0

// sembuf
struct sembuf pop, vop;
#define down(s) semop(s, &pop, 1)   // wait(s)
#define up(s) semop(s, &vop, 1)     // signal(s)


/*
    Function: create_shared_MTP_Table
    Arguments: None
    Return Value: Pointer to mtp_socket
    Workflow: Generates a shared memory key, creates or accesses shared memory segment for MTP table, attaches the segment, and returns the pointer to it.
*/
mtp_socket *create_shared_MTP_Table()
{
    int sm_key = ftok(".", KEY_MTP_TABLE);
    int sm_id = shmget(sm_key, (SIZE_SM)*sizeof(mtp_socket), 0777|IPC_CREAT);
    mtp_socket *MTP_Table = (mtp_socket *)shmat(sm_id, 0, 0);
    return MTP_Table;
}

/*
    Function: create_shared_variables
    Arguments: None
    Return Value: Pointer to shared_variables
    Workflow: Generates a shared memory key, creates or accesses shared memory segment for shared variables, attaches the segment, and returns the pointer to it.
*/
shared_variables *create_shared_variables()
{
    int sm_key = ftok(".", KEY_SHARED_RESOURCE);
    int sm_id_shared_vars = shmget(sm_key, sizeof(int), 0777|IPC_CREAT);
    shared_variables *vars = (shared_variables *)shmat(sm_id_shared_vars, 0, 0);
    return vars;
}

/*
    Function: create_entry_semaphore
    Arguments: Pointer to int id
    Return Value: None
    Workflow: Generates a semaphore key, creates or accesses a semaphore for entry, and stores its id in the provided pointer.
*/
void create_entry_semaphore(int *id)
{
    int sm_key = ftok(".", KEY_ENTRY_SEM);
    *id = semget(sm_key, 1, 0777|IPC_CREAT);
}

/*
    Function: create_exit_semaphore
    Arguments: Pointer to int id
    Return Value: None
    Workflow: Generates a semaphore key, creates or accesses a semaphore for exit, and stores its id in the provided pointer.
*/
void create_exit_semaphore(int *id)
{
    int sm_key = ftok(".", KEY_EXIT_SEM);
    *id = semget(sm_key, 1, 0777|IPC_CREAT);
}

/*
    Function: create_mtx_table_info
    Arguments: Pointer to int id
    Return Value: None
    Workflow: Generates a semaphore key, creates or accesses a semaphore for mutex table info, and stores its id in the provided pointer.
*/
void create_mtx_table_info(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
}

/*
    Function: create_mutex_swnd
    Arguments: Pointer to int id
    Return Value: None
    Workflow: Generates a semaphore key, creates or accesses a semaphore for mutex sliding window, and stores its id in the provided pointer.
*/
void create_mutex_swnd(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX_SWND);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
}

/*
    Function: create_mutex_recvbuf
    Arguments: Pointer to int id
    Return Value: None
    Workflow: Generates a semaphore key, creates or accesses a semaphore for mutex receive buffer, and stores its id in the provided pointer.
*/
void create_mutex_recvbuf(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX_RECVBUF);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
}

/*
    Function: create_mutex_sendbuf
    Arguments: Pointer to int id
    Return Value: None
    Workflow: Generates a semaphore key, creates or accesses a semaphore for mutex send buffer, and stores its id in the provided pointer.
*/
void create_mutex_sendbuf(int *id)
{
    int sm_key = ftok(".", KEY_MUTEX_SENDBUF);
    *id = semget(sm_key, 1, 0777 | IPC_CREAT);
}

/*
    Function: my_strcpy
    Arguments: char *a1, char *a2, int size
    Return Value: None
    Workflow: Copies characters from a2 to a1 up to the given size.
*/
void my_strcpy(char *a1, char *a2, int size)
{
    for (int i = 0; i < size; i++)
    {
        a1[i] = a2[i];
    }
    return;
}

/*
    Function: min_logical
    Arguments: int x, int y, int size
    Return Value: int
    Workflow: Calculates the minimum of x and y considering a logical wrap-around with a given size. 
              It first calculates the space between x and y, then compares it with half the size.
              If the space is less than half the size, it returns the minimum of x and y.
              Otherwise, it returns the maximum of x and y, considering the wrap-around.
*/
int min_logical(int x, int y, int size)
{
    int space = abs(x - y);
    if (space < size/2)
    {
        return (x <= y) ? x : y;
    }
    else
    {
        return (x <= y) ? y : x;
    }
}

int min(int a, int b)
{
    return (a>b) ? b : a;
}

/*
    Function: m_socket
    Arguments: int domain, int type, int protocol
    Return Value: int
    Workflow: Creates an MTP socket and initializes necessary shared resources. It first checks if the type of socket is SOCK_MTP.
              Then it initializes sembuf structures for P(s) and V(s), creates or accesses shared memory for MTP table and shared variables,
              creates entry and exit semaphores, and mutexes for table info. It iterates through the MTP table to find a free slot,
              sets up necessary information for the socket, and returns its ID. If there are no free slots, it returns an error.
*/
int m_socket(int domain, int type, int protocol)
{
    if(type != SOCK_MTP)
    {
        return ERR;
    }

    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1; 
    vop.sem_op = 1;

    mtp_socket *MTP_Table = create_shared_MTP_Table();
    shared_variables *shared_resource = create_shared_variables();

    int entry_sem;
    create_entry_semaphore(&entry_sem);

    int exit_sem;
    create_exit_semaphore(&exit_sem);

    int mtx_table_info;
    create_mtx_table_info(&mtx_table_info);

    down(mtx_table_info);
    for (int i = 0; i < SIZE_SM; i++)
    {
        if(MTP_Table[i].free == 1)
        {
            int user_mtp_id = i;
            MTP_Table[i].free = 0;
            MTP_Table[i].pid = getpid();
            
            shared_resource->status = 0;
            shared_resource->mtp_id = user_mtp_id;

            up(entry_sem);
            down(exit_sem);

            if(shared_resource->error_no!=0)
            {
                errno = shared_resource->error_no;
                MTP_Table[i].free = 1;
                up(mtx_table_info);
                return ERR;
            }
            
            shmdt(MTP_Table);
            shmdt(shared_resource);

            up(mtx_table_info);
            return user_mtp_id;
            
        }
    }
    errno = ENOBUFS; 
    up(mtx_table_info);   
    return ERR;    
}

/*
    Function: m_close
    Arguments: int socket_id
    Return Value: int
    Workflow: Closes the MTP socket associated with the given socket_id. It first initializes necessary shared resources,
              creates semaphores and mutexes, and locks the MTP table. It then clears the send and receive buffers, resets
              sliding window and receive window, updates status, and signals entry and exit semaphores. Afterward, it checks
              for any errors and returns the appropriate value.
*/
int m_close(int socket_id)
{
    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1; 
    vop.sem_op = 1;

    mtp_socket *MTP_Table = create_shared_MTP_Table();
    shared_variables *shared_resource = create_shared_variables();

    int entry_sem;
    create_entry_semaphore(&entry_sem);

    int exit_sem;
    create_exit_semaphore(&exit_sem);

    int mtx_table_info;
    create_mtx_table_info(&mtx_table_info);

    int mtx_swnd, mtx_recvbuf, mtx_sendbuf;
    create_mutex_recvbuf(&mtx_recvbuf);
    create_mutex_sendbuf(&mtx_sendbuf);
    create_mutex_swnd(&mtx_swnd);

    down(mtx_table_info);
    if(MTP_Table[socket_id].free == 0)
    {
        down(mtx_swnd);
        down(mtx_sendbuf);
        for (int k = 0; k < SEND_BUFFSIZE; k++)
        {
            MTP_Table[socket_id].send_buff[k].sequence_no = -1;
        }
        MTP_Table[socket_id].swnd.left_idx = 0;
        MTP_Table[socket_id].swnd.right_idx = (RECV_BUFFSIZE - 1) % SEND_BUFFSIZE;
        MTP_Table[socket_id].swnd.new_entry = 0;
        MTP_Table[socket_id].swnd.last_seq_no = 0;
        MTP_Table[socket_id].swnd.last_sent = -1;
        MTP_Table[socket_id].swnd.last_ack_seqno = 0;
        for (int k = 0; k < SWND_SIZE; k++)
        {
            MTP_Table[socket_id].swnd.last_active_time[k] = 0;
        }
        up(mtx_sendbuf);
        up(mtx_swnd);

        down(mtx_recvbuf);
        for (int k = 0; k < RECV_BUFFSIZE; k++)
        {
            MTP_Table[socket_id].recv_buff[k].sequence_no = -1;
        }
        for (int k = 0; k < RWND_SIZE; k++)
        {
            MTP_Table[socket_id].rwnd.window[k] = k + 1;
        }
        MTP_Table[socket_id].rwnd.nospace = 0;
        MTP_Table[socket_id].rwnd.last_inorder_received = 0;
        MTP_Table[socket_id].rwnd.last_user_taken = 0;
        up(mtx_recvbuf);

        MTP_Table[socket_id].free = 1;
        shared_resource->mtp_id = socket_id;
        
        shared_resource->status = 2;

        up(entry_sem);
        down(exit_sem);

        int retval = shared_resource->return_value;

        if(shared_resource->error_no!=0)
        {
            errno = shared_resource->error_no;
            MTP_Table[socket_id].free = 0;
            up(mtx_table_info);
            return retval;
        }


        shmdt(MTP_Table);
        shmdt(shared_resource);

        up(mtx_table_info);
        return retval;
    }
    up(mtx_table_info);
    return ERR;
}

/*
    Function: m_bind
    Arguments: int socket_id, char *src_ip, unsigned short int src_port, char *dest_ip, unsigned short int dest_port
    Return Value: int
    Workflow: Binds the given socket_id to a specific source and destination IP address and port. It initializes necessary shared resources,
              creates semaphores and mutexes, and locks the MTP table. It sets up the source and destination addresses in the MTP table,
              updates status, and signals entry and exit semaphores. Afterward, it checks for any errors and returns the appropriate value.
*/
int m_bind(int socket_id, char *src_ip, unsigned short int src_port, char *dest_ip, unsigned short int dest_port)
{
    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1; 
    vop.sem_op = 1;

    mtp_socket *MTP_Table = create_shared_MTP_Table();
    shared_variables *shared_resource = create_shared_variables();

    int entry_sem;
    create_entry_semaphore(&entry_sem);

    int exit_sem;
    create_exit_semaphore(&exit_sem);

    int mtx_table_info;
    create_mtx_table_info(&mtx_table_info);

    down(mtx_table_info);
    if(MTP_Table[socket_id].free == 0)
    {
        shared_resource->status = 1;
        shared_resource->mtp_id = socket_id;

        strcpy(MTP_Table[socket_id].dest_ip, dest_ip);
        MTP_Table[socket_id].dest_port = dest_port;

        shared_resource->src_addr.sin_addr.s_addr = inet_addr(src_ip);
        shared_resource->src_addr.sin_port = htons(src_port);
        shared_resource->src_addr.sin_family = AF_INET;


        up(entry_sem);
        down(exit_sem);

        int retval = shared_resource->return_value;

        if(shared_resource->error_no!=0)
        {
            errno = shared_resource->error_no;
            MTP_Table[socket_id].dest_ip[0] = '\0';
            MTP_Table[socket_id].dest_port = 0;
            up(mtx_table_info);
            return retval;
        }


        shmdt(MTP_Table);
        shmdt(shared_resource);

        up(mtx_table_info);
        return retval;
    }
    up(mtx_table_info);
    return ERR;
}

/*
    Function: m_sendto
    Arguments: int socket_id, char *buffer, int size, int flags, struct sockaddr *dest, int len
    Return Value: int
    Workflow: Sends data over the MTP socket to the specified destination. It initializes necessary shared resources,
              creates mutexes, and locks the send buffer and sliding window. It checks if the destination IP address and port
              match the stored values in the MTP table. If not, it returns an error. It then checks if there is space in the
              send buffer. If not, it returns an error. Otherwise, it copies the data to the send buffer, assigns a sequence
              number, and updates the sliding window. Afterward, it releases the locks and detaches shared memory.
*/
int m_sendto(int socket_id, char *buffer, int size, int flags, struct sockaddr *dest, int len)
{
    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1; 
    vop.sem_op = 1;

    mtp_socket *MTP_Table = create_shared_MTP_Table();
    shared_variables *shared_resource = create_shared_variables();

    int mtx_sendbuf;
    create_mutex_sendbuf(&mtx_sendbuf);

    int mtx_swnd;
    create_mutex_swnd(&mtx_swnd);

    struct sockaddr_in *dest_in = (struct sockaddr_in *)dest;
    char *given_dest_ip = inet_ntoa(dest_in->sin_addr);
    unsigned short given_dest_port = ntohs(dest_in->sin_port);

    if((strcmp(MTP_Table[socket_id].dest_ip, given_dest_ip)!=0) || (MTP_Table[socket_id].dest_port != given_dest_port))
    {
        errno = ENOTCONN;
        return ERR;
    }

    down(mtx_swnd);
    down(mtx_sendbuf);
    if(MTP_Table[socket_id].send_buff[MTP_Table[socket_id].swnd.new_entry].sequence_no != -1)
    {
        errno = ENOBUFS;
        up(mtx_sendbuf);
        up(mtx_swnd);
        shmdt(MTP_Table);
        shmdt(shared_resource);

        return ERR;
    }

    int idx = MTP_Table[socket_id].swnd.new_entry;
    my_strcpy(MTP_Table[socket_id].send_buff[idx].data, buffer, KB);
    MTP_Table[socket_id].send_buff[idx].sequence_no = (MTP_Table[socket_id].swnd.last_seq_no)%MAX_SEQ_NO + 1;
    MTP_Table[socket_id].swnd.last_seq_no = MTP_Table[socket_id].send_buff[idx].sequence_no;
    MTP_Table[socket_id].swnd.new_entry = (MTP_Table[socket_id].swnd.new_entry+1)%SEND_BUFFSIZE;

    shmdt(MTP_Table);
    shmdt(shared_resource);

    up(mtx_sendbuf);
    up(mtx_swnd);
}

/*
    Function: m_recvfrom
    Arguments: int socket_id, char *buffer, int size, int flags, struct sockaddr *dest, int *len
    Return Value: int
    Workflow: Receives data from the MTP socket. It initializes necessary shared resources, creates a mutex for the receive buffer,
              and locks it. It finds the minimum sequence number expected to be received and searches the receive buffer for data
              with that sequence number. If found, it copies the data to the buffer provided, updates the last user-taken sequence
              number, releases the lock, and returns the size of the data copied. If no message is available, it returns an error.
*/
int m_recvfrom(int socket_id, char *buffer, int size, int flags, struct sockaddr *dest, int *len)
{
    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1; 
    vop.sem_op = 1;

    mtp_socket *MTP_Table = create_shared_MTP_Table();
    shared_variables *shared_resource = create_shared_variables();

    int mtx_recvbuf;
    create_mutex_recvbuf(&mtx_recvbuf);

    down(mtx_recvbuf);
    int min_seqno = (MTP_Table[socket_id].rwnd.last_user_taken)%MAX_SEQ_NO+1;
    for(int i=0; i<RECV_BUFFSIZE; i++)
    {
        if(MTP_Table[socket_id].recv_buff[i].sequence_no == min_seqno)
        {
            my_strcpy(buffer, MTP_Table[socket_id].recv_buff[i].data, min(KB,size));
            MTP_Table[socket_id].recv_buff[i].sequence_no = -1;
            MTP_Table[socket_id].rwnd.last_user_taken = min_seqno;
            // if (MTP_Table[socket_id].rwnd.nospace==1)
            // {
            //     // no space
            // }
            shmdt(MTP_Table);
            shmdt(shared_resource);
            up(mtx_recvbuf);
            return KB;
        }
    }
    errno = ENOMSG;
    up(mtx_recvbuf);
    shmdt(MTP_Table);
    shmdt(shared_resource);
    return ERR;
}

/*
    Function: printTable
    Arguments: None
    Return Value: void
    Workflow: Prints the contents of the MTP table. It initializes necessary shared resources and creates a shared MTP table.
              Then it iterates through the table and prints the MTP_ID, PID, free status, and UDP socket ID for each entry.
*/
void printTable()
{
    mtp_socket *MTP_Table = create_shared_MTP_Table();
    printf("-----------------------------------------\n");
    printf("MTP_ID\tpid\tfree\tudp_sockid\n");
    for(int i=0; i<SIZE_SM; i++)
    {
        printf("%d\t%d\t%d\t%d\n", i, MTP_Table[i].pid, MTP_Table[i].free, MTP_Table[i].udp_sockid);
    }
    printf("-----------------------------------------\n");

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
    srand(time(NULL));
    float random_number = (float)(rand() )/RAND_MAX;
    
    if (random_number < p) 
    {
        return 1;
    }
    else
    {
        return 0;
    }
}


