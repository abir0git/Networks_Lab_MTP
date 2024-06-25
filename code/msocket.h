/*------------------- STANDARD LIBRARIES ---------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>
#include <fcntl.h>

/*----------------- MACROS -----------------*/
#define SOCK_MTP 115
#define P 0.1 // Probability of dropping a message

#define KEY_MTP_TABLE 100
#define KEY_SHARED_RESOURCE 35
#define KEY_ENTRY_SEM 23
#define KEY_EXIT_SEM 21
#define KEY_MUTEX 19
#define KEY_MUTEX_SWND 28
#define KEY_MUTEX_RECVBUF 89
#define KEY_MUTEX_SENDBUF 65

#define SIZE_SM 25       // Shared memory size
#define KB 1000          // Kilobyte size
#define IP_SIZE 20       // Maximum IP address size
#define SEND_BUFFSIZE 10 // Send buffer size
#define RECV_BUFFSIZE 5  // Receive buffer size
#define SWND_SIZE 5      // Send window size
#define RWND_SIZE 5      // Receive window size
#define MAX_SEQ_NO 16    // Maximum sequence number

#define TIMEOUT_S 4   // Timeout in seconds
#define TIMEOUT_US 0  // Timeout in microseconds
#define T 5           // Timeout time preiod and sleep time
#define GARBAGE_T 200 // G_Thread sleep time

/*------------------ STRUCTURES ----------------*/
typedef struct message
{
    int sequence_no; // Sequence number of the message
    char data[KB];   // Data payload of the message
} message;

typedef struct send_window
{
    int left_idx;                           // Left index of the send window
    int right_idx;                          // Right index of the send window
    int last_ack_seqno;                     // Last acknowledged sequence number
    int last_ack_emptyspace;                // Last acknowledged empty space in receive window
    int last_sent;                          // Index number of the last sent message
    time_t last_active_time[SEND_BUFFSIZE]; // Time of last activity for each message in the send buffer
    int new_entry;                          // Index number of the new entry in the send buffer
    int last_seq_no;                        // Last sequence number used in m-sendto(...) to keep track of message sequencing
} send_window;

typedef struct receive_window
{
    int window[5];             // Array representing the receive window
    int last_inorder_received; // Last message received in order
    int last_user_taken;       // Last message consumed by the user
    int nospace;               // Number of empty spaces in the receive window
} receive_window;

typedef struct mtp_socket
{
    int free;                         // Flag indicating if the MTP socket is free
    pid_t pid;                        // Process ID associated with the socket
    int udp_sockid;                   // UDP socket ID
    char dest_ip[IP_SIZE];            // Destination IP address
    unsigned short int dest_port;     // Destination port
    message send_buff[SEND_BUFFSIZE]; // Send buffer
    message recv_buff[RECV_BUFFSIZE]; // Receive buffer
    send_window swnd;                 // Send window
    receive_window rwnd;              // Receive window
} mtp_socket;

typedef struct shared_variables
{
    int status;                  // Status of the shared variables
    int mtp_id;                  // MTP ID associated with the shared variables
    struct sockaddr_in src_addr; // Source address

    int return_value; // Return value from functions
    int error_no;     // Error number associated with the shared variables
} shared_variables;

/*--------------- FUNCTION DECLARATIONS ---------------*/
int m_socket(int domain, int type, int protocol);
int m_bind(int socket_id, char *src_ip, unsigned short int src_port, char *dest_ip, unsigned short int dest_port);
int m_sendto(int socket_id, char *buffer, int size, int flags, struct sockaddr *dest, int len);
int m_recvfrom(int socket_id, char *buffer, int size, int flags, struct sockaddr *dest, int *len);
int m_close(int socket_id);

int dropMessage(float p); // Function to simulate dropping of messages based on a probability
