#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

// audio video talk
// hostname/ip
// port number
// username
// password
// reported interval in milliseconds
// packet size = 160

#define HEADERLEN 23

void usage(const char * message)
{
    fprintf(stderr,"%s\n",message);
    exit(1);
}

// 0-3 = MO_O
// 4-5 = command
// 6 = reserved
// 7-14 = reserved
// 15-18 = int32 text length
// 19-22 = reserved
// 23- = command text

// p1=buffer p2=cmd p3=len
ssize_t send_packet(int fd, unsigned char * buf, unsigned short cmd, size_t len)
{
    *(unsigned short *)(buf+4) = cmd;
    *(unsigned long *)(buf+15) = len;
    ssize_t x = send(fd, buf, len+HEADERLEN, 0);
    if (x < 0) {
        fprintf(stderr, "Failed to send command %u, %s\n",cmd,strerror(errno));
        exit(1);
    }
    return x;
}

// p1=buf p2=expected type p3=expected text length
ssize_t recv_packet(int fd, unsigned char * buf, unsigned short cmd, size_t len)
{
    ssize_t x = recv(fd, buf, HEADERLEN, 0);
    if (x != HEADERLEN) {
        fprintf(stderr, "Failed to recv command header %u %s\n",cmd,strerror(errno));
        exit(1);
    }
    if ((cmd != 65535) && (cmd != *(unsigned char *)(buf+4))) {
        fprintf(stderr, "RxCommand does not match want %u got %u\n",cmd,*(unsigned char *)(buf+4));
        exit(1);
    }

    unsigned int y = *(unsigned int*)(buf+15);
    if ((len != UINT_MAX) && (y != len)) {
        fprintf(stderr, "RxCommand len does not match %u got %u want %u\n",cmd,y,len);
        exit(1);
    }

    unsigned char * b = buf+HEADERLEN;
    while (y > 0) {
        x = recv(fd, b, y, 0);
        if (x < 1) {
            fprintf(stderr, "Failed to recv command body %u %s\n",cmd,strerror(errno));
            exit(1);
        }
        y -= x;
        b += x;
    }

    return b - buf - HEADERLEN;
}

int main(int argc, const char * argv[])
{
    if (argc < 5) { usage("not enough arguments"); }

    int fd_control = -1;
    int fd_stream  = -1;

    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int s = getaddrinfo(argv[2], argv[3], &hints, &result);
    if (s != 0) {
        usage(gai_strerror(s));
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd_control = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd_control == -1)
            continue;

        if (connect(fd_control, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */

        close(fd_control);
    }

    if (fd_control < 0) { usage(strerror(errno)); }
    fd_stream = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd_stream == -1)
        usage(strerror(errno));

    if (connect(fd_stream, rp->ai_addr, rp->ai_addrlen))
        usage(strerror(errno));

    // tx MO_O 0
    // rx MO_O 1
    // tx MO_O 2
    // rx MO_O 3
    // tx MO_O 16
    // rx MO_O 28 (big packet)?
    //
    // tx MO_O 4 start video
    // rx MO_O 17
    // rx MO_O 5 start video ack
    // tx MO_O 7 (1101) set framerate
    // stream
    // tx MO_O 6 (2628) stop video
    //
    // tx MO_O 11 (2632) start talk
    // rx MO_O 12 start talk ack
    // stream
    // tx MO_O 13 (2710) stop talk
    //
    // tx MO_O 8 (2869) start audio
    // rx MO_O 9 start audio ack
    // stream
    // tx MO_O 10 stop audio

    unsigned char buf[2000];
    unsigned char buf2[25000];

    memset(buf, 0, sizeof(buf));
    memset(buf2, 0, sizeof(buf2));

    buf[0]='M'; buf[1]='O'; buf[2]='_'; buf[3]='O';

    // login
    // tx MO_O 0
    // rx MO_O 1
    send_packet(fd_control, buf,0,0);
    ssize_t l = recv_packet(fd_control, buf,1,27);
    if (buf[HEADERLEN + 0] != 0) {
        fprintf(stderr, "Failed to initiate login\n");
        exit(1);
    }

    memset(buf+4, 0, sizeof(buf)-4);
    strncpy(buf+HEADERLEN,argv[4],13);
    strncpy(buf+HEADERLEN+13,argv[5],13);
    send_packet(fd_control, buf,2,26);
    l = recv_packet(fd_control, buf,3,3);
    if (buf[HEADERLEN + 0] != 0) {
        fprintf(stderr, "Failed to login %d\n",*(unsigned short *)(buf+HEADERLEN));
        exit(1);
    }

    // configure?
    memset(buf+4, 0, sizeof(buf)-4);
    send_packet(fd_control, buf,16,0);
    l = recv_packet(fd_control, buf,28,UINT_MAX);

    if (strcmp(argv[1],"talk")==0) {
        // start talk
        memset(buf+4, 0, sizeof(buf)-4);
        buf[HEADERLEN] = 1;
        send_packet(fd_control, buf,11,1);
        l = recv_packet(fd_control, buf,17,UINT_MAX);
        l = recv_packet(fd_control, buf,12,6);
        if (*(unsigned short *)(buf+HEADERLEN) != 0) {
            fprintf(stderr, "Start audio rejected %d\n",*(unsigned short *)(buf+HEADERLEN));
            exit(1);
        }

        buf[3] = 'V';
        unsigned int connection_id = *(unsigned int *)(buf+HEADERLEN+2);
        memset(buf+4, 0, sizeof(buf)-4);
        *(unsigned int *)(buf+HEADERLEN) = connection_id;
        send_packet(fd_stream, buf,0,4);
        usleep(950*1000);

        *(unsigned int *)(buf2+HEADERLEN-4) = 0xb1;
        unsigned int packetsize = 160;
        buf2[0]='M'; buf2[1]='O'; buf2[2]='_'; buf2[3]='V';
        unsigned int p = 0;
        *(unsigned int *)(buf2+HEADERLEN+13) = packetsize;
        struct timeval tv;
        while (1) {
            // TODO pacing for read from file
            usleep(40*1000);
            if ((l = read(0,buf2+HEADERLEN+17,packetsize)) < packetsize) {
                if (l == 0) { break; }
                memset(buf2+HEADERLEN+17+l,0,packetsize-l);
            }
            gettimeofday(&tv);
            unsigned int millis = tv.tv_usec/1000 + tv.tv_sec*1000;
            *(unsigned int *)(buf2+HEADERLEN+0 ) = millis;
            *(unsigned int *)(buf2+HEADERLEN+4 ) = p++;
            *(unsigned int *)(buf2+HEADERLEN+8 ) = tv.tv_sec;
            send_packet(fd_stream, buf2, 3, 177);
        }

        // stop talk
        memset(buf+4, 0, sizeof(buf)-4);
        buf[HEADERLEN] = 1;
        send_packet(fd_control, buf,13,0);
    }

    if (strcmp(argv[1],"audio") == 0) {
        // start audio
        memset(buf+4, 0, sizeof(buf)-4);
        buf[HEADERLEN] = 1;
        send_packet(fd_control, buf,8,1);
        l = recv_packet(fd_control, buf,12,6);
        if (*(unsigned short *)(buf+HEADERLEN) != 0) {
            fprintf(stderr, "Start audio rejected %d\n",*(unsigned short *)(buf+HEADERLEN));
            exit(1);
        }

        buf[3] = 'V';
        unsigned int connection_id = *(unsigned int *)(buf+HEADERLEN+2);
        memset(buf+4, 0, sizeof(buf)-4);
        *(unsigned int *)(buf+HEADERLEN) = connection_id;
        send_packet(fd_stream, buf,0,4);

        while (1) {
            recv_packet(fd_stream,buf,2,177);
            if (0) fprintf(stderr,"time %d package %d colltime %d len %d\n",
                   *(unsigned int *)(buf+HEADERLEN),
                   *(unsigned int *)(buf+HEADERLEN+4),
                   *(unsigned int *)(buf+HEADERLEN+8),
                   *(unsigned int *)(buf+HEADERLEN+13));
            write(1, buf+HEADERLEN+17, *(unsigned int *)(buf+HEADERLEN+13));
        }

        // stop audio
        buf[3] = 'O';
        memset(buf+4, 0, sizeof(buf)-4);
        buf[HEADERLEN] = 1;
        send_packet(fd_control, buf,10,0);
    }

    if (strcmp(argv[1],"video")==0) {
        // start video
        memset(buf+4, 0, sizeof(buf)-4);
        buf[HEADERLEN] = 1;
        send_packet(fd_control, buf,4,1);
        l = recv_packet(fd_control, buf,12,6);
        if (*(unsigned short *)(buf+HEADERLEN) != 0) {
            fprintf(stderr, "Start audio rejected %d\n",*(unsigned short *)(buf+HEADERLEN));
            exit(1);
        }

        buf[3] = 'V';
        unsigned int connection_id = *(unsigned int *)(buf+HEADERLEN+2);
        memset(buf+4, 0, sizeof(buf)-4);
        *(unsigned int *)(buf+HEADERLEN) = connection_id;
        send_packet(fd_stream, buf,0,4);

        buf[3] = 'O';
        memset(buf+4, 0, sizeof(buf)-4);
        //*(unsigned int *)(buf+HEADERLEN) = 10; // 20fps
        //*(unsigned int *)(buf+HEADERLEN) = 60; // 10fps
        *(unsigned int *)(buf+HEADERLEN) = 110; // 5fps
        //*(unsigned int *)(buf+HEADERLEN) = 150; // 1fps
        send_packet(fd_control, buf,7,4);

        while(1) {
            recv_packet(fd_stream,buf2,1,-1);
            if (0) printf("time %d colltime %d len %d\n",
                   *(unsigned int *)(buf2+HEADERLEN),
                   *(unsigned int *)(buf2+HEADERLEN+4),
                   *(unsigned int *)(buf2+HEADERLEN+9));
            write(1, buf2+HEADERLEN+13, *(unsigned int *)(buf2+HEADERLEN+9));
        }

        // stop audio
        memset(buf+4, 0, sizeof(buf)-4);
        buf[HEADERLEN] = 1;
        send_packet(fd_control, buf,6,0);
    }

    close(fd_stream);
    close(fd_control);
    exit(0);
}
