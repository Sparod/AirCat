/*
 * rtp_test.c
 *
 * A mini UDP proxy for stress test on RTP receiver:
 * - the reordering of packets,
 * - and the lost packets detection.
 *
 * Copyright (c) 2013   A. Dilly
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 8192

int launch_rtp_proxy(unsigned int in_port,
                     unsigned int out_port,
                     int misorder_p,
                     int lost_p)
{
    unsigned char buffer[BUFFER_SIZE];
    unsigned char mis[BUFFER_SIZE];
    struct sockaddr_in out_addr;
    struct sockaddr_in in_addr;
    int out_sock;
    int in_sock;
    size_t size;
    size_t mis_size = 0;
    int opt;
    int i = 0;
    int k = 0;
    int l = 0;

    /* Open socket */
    in_sock = socket(AF_INET, SOCK_DGRAM, 0);
    out_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(in_sock < 0 || out_sock < 0)
        return -1;

    /* Set low delay on UDP socket */
    opt = 6;
    if (setsockopt(in_sock, SOL_SOCKET, SO_PRIORITY, (const void *) &opt, sizeof(opt)) < 0)
        fprintf(stderr, "Can't change socket priority!\n");
    opt = 6;
    if (setsockopt(out_sock, SOL_SOCKET, SO_PRIORITY, (const void *) &opt, sizeof(opt)) < 0)
        fprintf(stderr, "Can't change socket priority!\n");

    /* Force socket to bind */
    opt = 1;
    if(setsockopt(in_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        return -1;

    /* Bind */
    memset(&in_addr, 0, sizeof(in_addr));
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(in_port);
    in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(in_sock, (struct sockaddr *) &in_addr, sizeof(in_addr)) != 0)
        return -1;

    /* Prepare Out address */
    memset(&out_addr, 0, sizeof(out_addr));
    out_addr.sin_family = AF_INET;
    out_addr.sin_port = htons(out_port);
    if(inet_aton("127.0.0.1", &out_addr.sin_addr) == 0)
        return -1;

    /* Wait for next packet */
    while(1)
    {
        /* Receive packet */
        size = recv(in_sock, buffer, BUFFER_SIZE, 0);
        if(size <= 0)
            continue;

        /* Lost packet */
        if(lost_p > 0 && i >= lost_p)
        {
            i = 0;
            printf("Packet Lost\n");
            continue;
        }
        i++;

        /* Misorder packet */
        if(misorder_p > 0 && k >= misorder_p)
        {
            printf("Pick a packet...\n");
            memcpy(mis, buffer, size);
            mis_size = size;
            l = 0;
            k = 0;
            continue;
        }
        k++;

        /* Send packet */
        sendto(out_sock, buffer, size, 0, (struct sockaddr *) &out_addr, sizeof(out_addr));

        /* Send misorder packet */
        if(mis_size > 0)
        {
            if(l >= 4)
            {
                sendto(out_sock, mis, mis_size, 0, (struct sockaddr *) &out_addr, sizeof(out_addr));
                mis_size = 0;
                printf("Release a packet\n");
                l = 0;
            }
            l++;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int in, out, mis, lost;

    /* Verify arguments */
    if(argc < 5)
    {
        fprintf(stderr, "Usage: %s IN_PORT OUT_PORT MISORDER LOST\n", argv[0]);
        return -1;
    }

    /* Get argument values */
    in = atoi(argv[1]);
    out = atoi(argv[2]);
    mis = atoi(argv[3]);
    lost = atoi(argv[4]);
    
    /* Launch proxy */
    launch_rtp_proxy(in, out, mis, lost);

    return 0;
}
