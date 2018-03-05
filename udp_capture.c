/* Capture UDP Multicast content to a file */

#include <string.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>

#include <semaphore.h>
#include <errno.h>

#ifdef __cplusplus
}
#endif


//#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <sys/ioctl.h>

#include <string.h>
#include "CWrapperThread.h"
#include <queue>

//#define DO_OPEN_CLSOE

#define MAX_NO_OF_BUFFERS               (20000)
#define UDP_CAPTURE_BUFFER_SIZE         1518/* (188 * 7) */

#define FILE_WRITE_BUFF_SIZE            (64 * UDP_CAPTURE_BUFFER_SIZE)

#define UDP_CAPTURE_INIT_MUTEX(_X_)     (sem_init(_X_, 0, 1))
#define UDP_CAPTURE_DESTROY_MUTEX(_X_)  (sem_destroy(_X_))
#define UDP_CAPTURE_LOCK_MUTEX(_X_)     (sem_wait(_X_))
#define UDP_CAPTURE_UNLOCK_MUTEX(_X_)   (sem_post(_X_))

using namespace std;

typedef struct tagBufferInfo
{
    unsigned char   m_aui8Buff[UDP_CAPTURE_BUFFER_SIZE];
    int             m_i32DataLen;
} BufferInfo;

typedef struct tagUDPCaptureContext
{
    struct sockaddr_in  m_localSock;
    struct ip_mreq      m_group;
    int                 m_sd;

    bool                bStopWriteThread;
    FILE                *m_fpOut;
    sem_t               m_WriteBufReadySignalSem;
    
    BufferInfo          m_astBuffers[MAX_NO_OF_BUFFERS];

    sem_t                    m_bufferQueueMuteX;
    std::queue<BufferInfo*>  m_bufferQueue;

    sem_t                    m_WriteQueueMuteX;
    std::queue<BufferInfo*>  m_writeQueue;
} UDP_CAPTURE_CONTEXNT;

UDP_CAPTURE_CONTEXNT    gstUdpCaptureCtxt;


static void *UDP_CAPTURE_FileWriteThread(void *ctxt)
{
    UDP_CAPTURE_CONTEXNT    *pstCtxt = (UDP_CAPTURE_CONTEXNT*) ctxt;
    unsigned char           * pi8LocalBuf = new unsigned char[FILE_WRITE_BUFF_SIZE];
    unsigned char           * pTmp;
    unsigned int              i32Copied;

    if(NULL == pi8LocalBuf)
    {
        perror("Malloc failure for File Write Buff...");
        exit(1);
    }

    i32Copied = 0;
    while(1)
    {
        // Wait for signal
        UDP_CAPTURE_LOCK_MUTEX(&pstCtxt->m_WriteBufReadySignalSem);

        if(pstCtxt->bStopWriteThread)
        {
            break;
        }

        if(0 == i32Copied)
        {
            pTmp = pi8LocalBuf;
        }

        while (1)
        {
            BufferInfo* pBuf;

            UDP_CAPTURE_LOCK_MUTEX(&pstCtxt->m_WriteQueueMuteX);
            if(pstCtxt->m_writeQueue.empty())
            {
                UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_WriteQueueMuteX);
                break;
            }
            pBuf = pstCtxt->m_writeQueue.front();
            if(pBuf)
            {
                pstCtxt->m_writeQueue.pop();
            }
            UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_WriteQueueMuteX);

            if(pBuf)
            {
                if(i32Copied >= FILE_WRITE_BUFF_SIZE)
                {
                    fwrite(pi8LocalBuf, 1, i32Copied, pstCtxt->m_fpOut);
                    i32Copied = 0;
                    pTmp = pi8LocalBuf;
                }

                if(0 == i32Copied)
                {
                    pTmp = pi8LocalBuf;
                }
                
                if((i32Copied + pBuf->m_i32DataLen) <= FILE_WRITE_BUFF_SIZE)
                {
                    memcpy(pTmp, pBuf->m_aui8Buff, pBuf->m_i32DataLen);
                    pTmp += pBuf->m_i32DataLen;
                    i32Copied += pBuf->m_i32DataLen;
                }
                else
                {
                    fwrite(pi8LocalBuf, 1, i32Copied, pstCtxt->m_fpOut);
                    i32Copied = 0;
                    pTmp = pi8LocalBuf;

                    memcpy(pTmp, pBuf->m_aui8Buff, pBuf->m_i32DataLen);
                    pTmp += pBuf->m_i32DataLen;
                    i32Copied += pBuf->m_i32DataLen;
                }

                UDP_CAPTURE_LOCK_MUTEX(&pstCtxt->m_bufferQueueMuteX);
                pstCtxt->m_bufferQueue.push(pBuf);
                UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_bufferQueueMuteX);
            }
            else
            {
                printf(" ----- NULL BUFF in Write\n\n");
            }
        }
    }

    if(i32Copied)
    {
        fwrite(pi8LocalBuf, 1, i32Copied, pstCtxt->m_fpOut);
    }
    fflush(pstCtxt->m_fpOut);

    pthread_exit(NULL);
    return 0;
}

#ifdef __cplusplus
extern "C" {
#endif

/**
  * simple function to wait for data ready event from the socket
 **/
int UDP_CAPTURE_WaitForSocketData(UDP_CAPTURE_CONTEXNT *pstCtxt, bool *recvTimeout)
{
    int rc;
    int fd;
    fd_set rfds;
    struct timeval tv;
    int dbgCnt = 0;
    int result = -1;
    static int logError = 0;

    fd = pstCtxt->m_sd;
    *recvTimeout = false;
    for(;;) {
        if (pstCtxt->bStopWriteThread)
        {
            goto done;
        }

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        if (1/*playback_ip->disableLiveMode*/)
        {
            /* increase the socket timeout as scaled data may take longer to arrive */
            tv.tv_sec = 30;
            tv.tv_usec = 0;
            //printf("Live mode is disabled, so using the default large timeout of %d sec\n", tv.tv_sec);
        }
        else
        {
            //tv.tv_sec = 0;
            //tv.tv_usec = (playback_ip->settings.maxNetworkJitter + 100) * 1000; /* converted to micro sec */
        }

        if ( (rc = select(fd +1, &rfds, NULL, NULL, &tv)) < 0 && rc != EINTR)
        {
            printf("%s: ERROR: select(): errno = %d, fd %d", __FUNCTION__, errno, fd);
            goto done;
        }
        else if (rc == EINTR)
        {
            printf("select returned EINTR, continue the loop");
            continue;
        }
        else if (rc == 0)
        {
            if (dbgCnt++ % 1 == 0)
                printf("%s: timed out in waiting for data from server (fd %d, timeout %d): coming out of select loop",
                            __FUNCTION__, fd, 1/*playback_ip->settings.maxNetworkJitter*/);
            *recvTimeout = true;
            //playback_ip->numRecvTimeouts++;
            logError = 1;
            result = 0;
            break;
        }
        else {
            *recvTimeout = false;
        }

        if (!FD_ISSET(fd, &rfds)) {
            /* some select event but our FD not set: No more data - wait */
            continue;
        }

        /* there is some data in the socket */
        result = 0;
        if (logError) {
            printf("%s: Network Data is now coming again", __FUNCTION__);
            logError = 0;
        }
        break;
    }
done:
    return result;
}


int GetUDPData(
    UDP_CAPTURE_CONTEXNT *pstCtxt,
    void *buf,       /* buffer big enough to hold (bufSize * iterations) bytes */
    int bufSize,     /* how much to read per iteration */
    int iterations, /* number of iterations (i.e. recv calls) */
    int *bytesRecv,    /* pointer to list of bytesRecv for each recvfrom call */
    int *totalBytesRecv, /* total bytes received */
    bool *recvTimeout   /* set if timeout occurs while waiting to receive a IP packet (helps detect unmarked discontinuity */
    )
{
    int i;
    int fd;
    int bytesRead = 0;
    char *bufp;

    fd = pstCtxt->m_sd;
    bufp = (char *)buf;
    *totalBytesRecv = 0;

    for (i=0; i< iterations; i++)
    {
        /* keep waiting until there is enough data read for consumption in the socket */
        if (UDP_CAPTURE_WaitForSocketData(pstCtxt, recvTimeout) != 0)
            goto error;

        if (*recvTimeout == true) {
            /* reset the timeout flag if we have received any data, this allows caller to process this data */
            /* this can only happen for normal sockets, where we have to make multiple recvfrom calls to receive a chunk */
            /* next read will hit the timeout flag if there is a true n/w loss event */
            if (*totalBytesRecv != 0)
                *recvTimeout = false;
            goto out;
        }

        /* read from the socket & check errors */
        //printf("%s: calling recvfrom fd %d; bytes %d", __FUNCTION__, fd, bufSize );
        bytesRead = recvfrom(fd, bufp, bufSize, 0, NULL, NULL);
        if (bytesRead  < 0  && errno != EINTR && errno != EAGAIN)
        {
            printf("%s: recvfrom ERROR: errno = %d", __FUNCTION__, errno);
            goto error;
        }
        else if (bytesRead == 0) {
            printf("%s: No more data from Server, we are done!", __FUNCTION__);
            goto error;
        }
        else if (bytesRead < 0 && (errno == EINTR || errno == EAGAIN)) {
            printf("%s: Recvfrom System Call interrupted or timed out (errno = %d), retry it\n", __FUNCTION__, errno);
            continue;
        }
        //printf("%s: Received %d bytes\n", __FUNCTION__, bytesRead);
        *bytesRecv = bytesRead;
        bufp += bytesRead;
        *totalBytesRecv += bytesRead;
        bytesRecv++;
    }

out:
    return 0;
error:
    return 1;
}

#ifdef __cplusplus
}
#endif


#define NEW_RMEM_MAX                  (1*256*1024)
void TuneNetwork(int fd)
{
    int     new_rmem_max = NEW_RMEM_MAX;
    /*
     * Linux kernel tuning: set socket receive buffer to 100k by default.
     * It works with low bit rate stream up to ~14Mbps. With higher bit rate (19 or above)Mbps,
     * Linux starts dropping UDP packets. We need to increase our UDP socket receive buffer size
     * by using setsockopt(). Therefore, /proc/sys/net/core/rmem_max needs to be changed.
     */
    FILE    *f;
    char    buf[80];
    int     rmax;
    int size = new_rmem_max;
    socklen_t len;
    size_t tmpLen;

    f = fopen("/proc/sys/net/core/rmem_max", "rw");
    if (f) {
        tmpLen = fread(buf, 1, sizeof(buf)-1, f);
        buf[tmpLen] = '\0';
        rmax = strtol(buf, (char **)NULL, 10);
        fclose(f);
        printf("*** rmem_max %d\n", rmax);
        if (rmax < NEW_RMEM_MAX) {
            /* it is the default value, make it bigger */
            printf("%s: Increasing default rmem_max from %d to rmem_max %d\n", __FUNCTION__, rmax, NEW_RMEM_MAX);
            tmpLen = snprintf(buf, sizeof(buf), "%d", NEW_RMEM_MAX);
            f = fopen("/proc/sys/net/core/rmem_max", "w");
            if (f) {
                fwrite(buf, 1, tmpLen, f);
                fclose(f);
                new_rmem_max = NEW_RMEM_MAX;
            } else
                new_rmem_max = rmax;
        } else
            new_rmem_max = rmax;
    }

    /* now increase socket receive buffer size */

    len = sizeof(size);
    if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, &len) == 0)
        printf("%s: current socket receive buffer size = %d KB\n", __FUNCTION__, size/1024);
    if (size < new_rmem_max) {
        size = new_rmem_max;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, len) != 0)
            printf("%s: ERROR: can't set UDP receive buffer to %d, errno=%d\n", __FUNCTION__, size, errno);
        len = sizeof(size);
        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, &len) == 0)
            printf("%s: new socket receive buffer size = %d KB\n", __FUNCTION__, size/1024);
    }
}

int main(int argc, char *argv[])
{
    UDP_CAPTURE_CONTEXNT    *pstCtxt = &gstUdpCaptureCtxt;
    CWrapperThread  fileWriteThread;
    char *pui8Addr = argv[1];
    int i32Port = atoi(argv[2]);
    int noOfBytesToRead = atoi(argv[3]);
    uint32_t noOfBytesRead;
    uint32_t noOfBytesProcessed = 0;
    unsigned int ui32Loop;

    printf("pui8Addr - %s, i32Port - %d No.Of bytes to read - %d\n", pui8Addr, i32Port, noOfBytesToRead);

    for(ui32Loop = 0; ui32Loop < MAX_NO_OF_BUFFERS; ++ui32Loop)
    {
        pstCtxt->m_bufferQueue.push(&pstCtxt->m_astBuffers[ui32Loop]);
    }

    //pstCtxt->m_bufferQueue.resize(MAX_NO_OF_BUFFERS);
    //pstCtxt->m_writeQueue.resize(MAX_NO_OF_BUFFERS);

    /* Create a datagram socket on which to receive. */
    pstCtxt->m_sd = socket(AF_INET, SOCK_DGRAM, 0);
    if(pstCtxt->m_sd < 0)
    {
        perror("Opening datagram socket error");
        exit(1);
    }
    else
    {
        printf("Opening datagram socket....OK.\n");
    }

    /* Enable SO_REUSEADDR to allow multiple instances of this */
    /* application to receive copies of the multicast datagrams. */
    {
        int reuse = 1;

        if(setsockopt(pstCtxt->m_sd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0)

        {
            perror("Setting SO_REUSEADDR error");
            close(pstCtxt->m_sd);
            exit(1);
        }
        else
        {
            printf("Setting SO_REUSEADDR...OK.\n");
        }
    }

    /* Bind to the proper port number with the IP address */
    /* specified as INADDR_ANY. */
    memset((char *) &pstCtxt->m_localSock, 0, sizeof(pstCtxt->m_localSock));
    pstCtxt->m_localSock.sin_family = AF_INET;
    pstCtxt->m_localSock.sin_port = htons(i32Port);
    pstCtxt->m_localSock.sin_addr.s_addr = INADDR_ANY;
    if(bind(pstCtxt->m_sd, (struct sockaddr*)&pstCtxt->m_localSock, sizeof(pstCtxt->m_localSock)))
    {
        perror("Binding datagram socket error");
        close(pstCtxt->m_sd);
        exit(1);
    }
    else
    {
        printf("Binding datagram socket...OK.\n");
    }

    /* Join the multicast group 226.1.1.1 on the local 203.106.93.94 */
    /* interface. Note that this IP_ADD_MEMBERSHIP option must be */
    /* called for each local interface over which the multicast */
    /* datagrams are to be received. */
    pstCtxt->m_group.imr_multiaddr.s_addr = inet_addr(pui8Addr);
    pstCtxt->m_group.imr_interface.s_addr = htonl(INADDR_ANY); //inet_addr("10.89.13.122");
    if(setsockopt(pstCtxt->m_sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&pstCtxt->m_group, sizeof(pstCtxt->m_group)) < 0)
    {
        perror("Adding multicast group error");
        close(pstCtxt->m_sd);
        exit(1);
    }
    else
    {
        printf("Adding multicast group...OK.\n");
    }

    pstCtxt->m_fpOut = fopen("udp_capture.ts", "wb");
    if(!pstCtxt->m_fpOut)
    {
        perror("Failed to open output file");
        close(pstCtxt->m_sd);
        exit(1);
    }

    else
    {
        //printf("Dump file open...OK\n");
    }
#ifdef DO_OPEN_CLSOE
    fclose(pstCtxt->m_fpOut);
#endif /* DO_OPEN_CLSOE */

    TuneNetwork(pstCtxt->m_sd);


    UDP_CAPTURE_INIT_MUTEX(&pstCtxt->m_bufferQueueMuteX);
    UDP_CAPTURE_INIT_MUTEX(&pstCtxt->m_WriteQueueMuteX);

    UDP_CAPTURE_INIT_MUTEX(&pstCtxt->m_WriteBufReadySignalSem);
    UDP_CAPTURE_LOCK_MUTEX(&pstCtxt->m_WriteBufReadySignalSem);
    pstCtxt->bStopWriteThread = false;
    fileWriteThread.StartThread(UDP_CAPTURE_FileWriteThread, pstCtxt, "UDP Capture File Write Thread");

    noOfBytesProcessed = 0;
get_data:
    while(noOfBytesProcessed < noOfBytesToRead)
    {
        BufferInfo* pBuf;
        bool recvTimeout = false;

        UDP_CAPTURE_LOCK_MUTEX(&pstCtxt->m_bufferQueueMuteX);
        if(pstCtxt->m_bufferQueue.empty())
        {
            printf("Insufficient buffers - increase it .........\n");
            UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_bufferQueueMuteX);
            continue;
        }
        pBuf = pstCtxt->m_bufferQueue.front();
        pstCtxt->m_bufferQueue.pop();
        UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_bufferQueueMuteX);

        //printf(" buffer - %d, write - %d\n", pstCtxt->m_bufferQueue.size(), pstCtxt->m_writeQueue.size());

        if(pBuf)
        {
            int bytesPerPacket = sizeof(pBuf->m_aui8Buff);

            /* Read from the socket. */
            pBuf->m_i32DataLen = 0;
            //if((pBuf->m_i32DataLen = read(pstCtxt->m_sd, pBuf->m_aui8Buff, sizeof(pBuf->m_aui8Buff))) < 0)
            if(GetUDPData(pstCtxt, pBuf->m_aui8Buff, sizeof(pBuf->m_aui8Buff), 1, &bytesPerPacket, &pBuf->m_i32DataLen, &recvTimeout) != 0)
            {
                perror("Reading datagram message error");
                pBuf->m_i32DataLen = 0;
                //close(pstCtxt->m_sd);
                //fclose(pstCtxt->m_fpOut);
                //exit(1);
            }
            else
            {
                //if(pBuf->m_i32DataLen < sizeof(pBuf->m_aui8Buff))
                //{
                //    printf("\nUnlign read - %d\n", pBuf->m_i32DataLen);
                //}
            }
            //printf("\nbytesPerPacket - %d, pBuf->m_i32DataLen - %d\n", bytesPerPacket, pBuf->m_i32DataLen);
            noOfBytesProcessed += pBuf->m_i32DataLen;
            UDP_CAPTURE_LOCK_MUTEX(&pstCtxt->m_WriteQueueMuteX);
            pstCtxt->m_writeQueue.push(pBuf);
            UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_WriteQueueMuteX);
            UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_WriteBufReadySignalSem);
        }
        else
        {
            printf("Insufficient buffers - increase it .........\n");
        }
    }
    printf("UDP Capture Complete - %d bytes ...\n", noOfBytesProcessed);

    pstCtxt->bStopWriteThread = true;
    UDP_CAPTURE_UNLOCK_MUTEX(&pstCtxt->m_WriteBufReadySignalSem);
    fileWriteThread.StopThread();

    UDP_CAPTURE_DESTROY_MUTEX(&pstCtxt->m_bufferQueueMuteX);
    UDP_CAPTURE_DESTROY_MUTEX(&pstCtxt->m_WriteQueueMuteX);
    UDP_CAPTURE_DESTROY_MUTEX(&pstCtxt->m_WriteBufReadySignalSem);

    close(pstCtxt->m_sd);
#ifndef DO_OPEN_CLSOE
    fflush(pstCtxt->m_fpOut);
    fclose(pstCtxt->m_fpOut);
#endif /* DO_OPEN_CLSOE */

    return 0;
}



