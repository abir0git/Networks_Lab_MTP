#include "msocket.h"

#define MAX_BUFFER_SIZE KB
#define FILE_NAME "test_file_2.txt"
#define MAX_SEND_SIZE KB

#define IP_1 "127.0.0.1"
#define PORT_1 2001
#define IP_2 "127.0.0.1"
#define PORT_2 3001

int main()
{
    int id1 = m_socket(AF_INET, SOCK_MTP, 0);
    if(id1 < 0)
    {
        perror("socket");
    }
    printf("Msocket id : %d\n", id1);

    if (m_bind(id1, IP_1, PORT_1, IP_2, PORT_2) < 0)
    {
        perror("bind");
    }

    char buffer[KB];

    struct sockaddr_in dest;
    dest.sin_addr.s_addr = inet_addr(IP_2);
    dest.sin_port = htons(PORT_2);
    dest.sin_family = AF_INET;
    int len = sizeof(dest);


    FILE *file = fopen(FILE_NAME, "rb");
    if (file == NULL)
    {
        perror("File open failed");
        exit(EXIT_FAILURE);
    }

    // Read and send file contents in chunks of 1KB
    size_t bytes_read;
    ssize_t bytes_sent;
    int i = 0;
    while ((bytes_read = fread(buffer, sizeof(char), KB, file)) > 0)
    {
        i++;
        // Send the chunk of data
        while (m_sendto(id1, buffer, KB, 0, (struct sockaddr *)&dest, sizeof(dest)) < 0)
        {
            
        }
        printf("Sent message chunk: %d\n", i);
        for(int j=0; j<KB; j++)
            buffer[j] = '\0';
    }
    i++;
    buffer[0] = '#';
    while (m_sendto(id1, buffer, KB, 0, (struct sockaddr *)&dest, sizeof(dest)) < 0)
    {
        
    }
    printf("Sent last message chunk: %d\n", i);


    // Close file and socket
    fclose(file);


    printf("File '%s' sent successfully.\n", FILE_NAME);

    return 0;
}