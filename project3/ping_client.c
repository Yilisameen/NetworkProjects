#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <getopt.h>
#include <pthread.h>
#include <limits.h>

typedef struct{
    //uint8_t type;
    //uint8_t code;
    uint16_t tc;//used for checksum_calculate test;
    uint16_t checksum;
    uint16_t id;
    uint16_t seqno;
    uint8_t timestamp[6];//?????
}Datagram;

struct option long_options[]={
    {"server_ip",required_argument,NULL,1},
    {"server_port",required_argument,NULL,2},
    {"count",required_argument,NULL,3},
    {"period",required_argument,NULL,4},
    {"timeout",required_argument,NULL,5},
};

typedef struct{
    //int sockfd;
    int seqno;
    int id;
    char *address;
    char *port;
    struct timeval timeout;
    int *flag;
    int *rrt;
}Param;

uint64_t htonll(uint64_t _hostlong)
{
    uint64_t result = htonl(_hostlong);
    return (result << 32 | htonl(_hostlong >> 32));
}

uint64_t ntohll(uint64_t _netlong)
{
    uint64_t result = ntohl(_netlong);
    return (result << 32 | ntohl(_netlong >> 32));
}


//check the checksum of the data received
int checksum_check(void* datagram){
    int res;//if res=1, checksum is right; if res=0, checksum is wrong;
    uint8_t *datafield=(uint8_t *)datagram;
    uint32_t sum=0x0;
    for(size_t i=0; i+2<=14;i=i+2){
        uint16_t d;
        memcpy(&d, datafield+i, 2);
        sum=sum + ntohs(d);
        if(sum>0xffff){
            sum=sum-0xffff;
        }
    }
    uint16_t sums=sum;
    if(sums==0xffff)
        res=1;
    else
        res=0;
    return res;
}

//calculate the checksum of thevim data to send
uint16_t checksum_calculate(void *datagram){
    uint8_t *datafield=(uint8_t *)datagram;//for calculation convenience
    uint32_t checksum=0x0;
    
    for(size_t i = 0; i+2 <= 14; i = i+2 ){//the length of the datagram is 14 bytes
        uint16_t d;
        memcpy(&d, datafield+i, 2);
        checksum=checksum + ntohs(d);
        if(checksum>0xffff){
            checksum=checksum-0xffff;
        }
    }
    return htons(~checksum);
}

//pack the message
Datagram ping_message(int id, int seqno, unsigned long long timestamp){
    Datagram datagram;
    datagram.tc=0x08;
    datagram.checksum=htons(0);
    datagram.id=htons(id);
    datagram.seqno=htons(seqno);
    uint64_t d=htonll(timestamp);
    memcpy((void *)datagram.timestamp, (void *)&d+2, 6);
    datagram.checksum=checksum_calculate((void *)&datagram);
    return datagram;
}


//calculate time_stamp
unsigned long long time_stamp(void){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned long long ms=(unsigned long long)(tv.tv_sec)*1000+(unsigned long long)(tv.tv_usec)/1000;
    return ms;
}

//listen to reply
void *ping(void *args){
    //
    Param *param=(Param *)args;
    int seqno=param->seqno;
    int id=param->id;
    char *address=param->address;
    char *port=param->port;
    struct timeval timeout=param->timeout;
    
    struct sockaddr_in their_addr;
    Datagram data_receive;
    socklen_t addr_len=sizeof(their_addr);
    ssize_t data_len=sizeof(data_receive);
    int numbytes;
    struct timeval timer=timeout;
    fd_set readfds;
    
    
    //
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    //int numbytes;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;//Use UDP protocol
    
    rv=getaddrinfo(address, port, &hints, &servinfo);
    if(rv!=0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL;
    }
    
    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return NULL;
    }
    
    //pack datagram
    unsigned long long timestamp=time_stamp();
    Datagram datagram=ping_message(id, seqno, timestamp);
    
    //send the datagram
    numbytes=sendto(sockfd, &datagram, sizeof(datagram), 0, p->ai_addr, p->ai_addrlen);
    if(numbytes == -1){
        perror("talker: sendto");
        exit(1);
    }
    
    
    FD_ZERO(&readfds);
    FD_SET(sockfd,&readfds);
    
    while(1){
        select(sockfd+1, &readfds, NULL, NULL, &timer);
        if(FD_ISSET(sockfd,&readfds)){
            numbytes=recvfrom(sockfd, &data_receive, data_len, 0, (struct sockaddr *)&their_addr, &addr_len);
            if(numbytes==-1){
                perror("recvfrom");
                exit(1);
            }
            unsigned long long endtimestamp=time_stamp();
            if(checksum_check(&data_receive)){
                uint64_t d=0;
                memcpy((void *)&d+2, data_receive.timestamp, 6);
                d=ntohll(d);
                int t=(int)(endtimestamp - (unsigned long long)d);
                printf("PONG %s: seq=%d time= %d ms\n",(char *)inet_ntoa(their_addr.sin_addr), ntohs(data_receive.seqno),t);
                param->flag[ntohs(data_receive.seqno)]=1;
                param->rrt[ntohs(data_receive.seqno)]=t;
                break;
            }
            else
                continue;//printf("Reply with wrong ckecksum!\n");
        }
        else
            break;
    }
    return NULL;
}



void Sleep(int ms){
    struct timeval delay;
    delay.tv_sec=0;
    delay.tv_usec=ms*1000;
    select(0, NULL, NULL, NULL, &delay);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int main(int argc, const char * argv[]) {
    //Initialization:
    int count=0;//number of pings to send
    int period=0;//period in miliseconds
    struct timeval timeout;//timeout in milliseconds
    timeout.tv_sec=0;
    char *address=malloc(100*sizeof(char));
    char *port=malloc(100*sizeof(char));
    int opt;
    int option_index=0;
    char *string="1:2:3:4:5:";
    while((opt=getopt_long(argc, argv, string, long_options, &option_index))!=-1){
        switch (opt) {
            case 1:
                memcpy(address, optarg, 100);
                break;
            case 2:
                memcpy(port, optarg, 100);
                break;
            case 3:
                count=atoi(optarg);
                break;
            case 4:
                period=atoi(optarg);
                break;
            case 5:
                timeout.tv_usec=1000*atoi(optarg);
                break;
            default:
                break;
        }
    }
    
    
    
    //open socket
    /*int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;//Use UDP protocol
    
    rv=getaddrinfo(address, port, &hints, &servinfo);
    if(rv!=0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return 2;
    }*/
    
    //get id:
    pid_t pid=getpid();
    int id=(int)pid;
    int *flag=malloc((count+1)*sizeof(int));
    int *rrt=malloc((count+1)*sizeof(int));
    for(int i=0;i<count+1;i++){
        flag[i]=0;
        rrt[i]=0;
    }
    Param *param=malloc((count+1)*sizeof(Param));
    pthread_t *t=malloc((count+1)*sizeof(pthread_t));
    void *status;
    
    printf("PING %s...\n", address);
    
    
    unsigned long long st=time_stamp();
    int cnt=1;

    while(cnt<=count){
        param[cnt].seqno=cnt;
        param[cnt].id=id;
        param[cnt].address=address;
        param[cnt].port=port;
        param[cnt].timeout=timeout;
        param[cnt].flag=flag;
        param[cnt].rrt=rrt;
        /*unsigned long long timestamp=time_stamp();
        Datagram datagram=ping_message(id, cnt, timestamp);
        
        //send the datagram
        numbytes=sendto(sockfd, &datagram, sizeof(datagram), 0, p->ai_addr, p->ai_addrlen);
        if(numbytes == -1){
            perror("talker: sendto");
            exit(1);
        }*/
        pthread_create(&t[cnt], NULL, ping, (void *)&param[cnt]);
        
        cnt++;
        if(cnt<=count)
            Sleep(period);
        
    }
    
    for(int i=1;i<=count;i++){
        pthread_join(t[i], &status);
    }
    
    unsigned long long et=time_stamp();
    
    int total_time=(int)(et-st);
    int sum_rrt=0;
    int max_rrt=INT_MIN;
    int min_rrt=INT_MAX;
    int received=0;
    for(int i=1;i<=count;i++){
        if(flag[i]){
            received++;
            sum_rrt+=rrt[i];
            max_rrt=max_rrt > rrt[i] ? max_rrt : rrt[i];
            min_rrt=min_rrt < rrt[i] ? min_rrt : rrt[i];
        }
    }
    if(max_rrt==INT_MIN)
        max_rrt=0;
    if(min_rrt==INT_MAX)
        min_rrt=0;
    int avg=0;
    if(received!=0)
        avg=sum_rrt/received;
    int loss_precentage = (count-received)*100/count;
    printf("--- %s ping statistics ---\n",address);
    printf("%d transmitted, %d received, %d%% loss, time %d ms\n", count, received, loss_precentage, total_time);
    printf("rtt min/avg/max = %d/%d/%d\n", min_rrt, avg, max_rrt);
    
    return 0;
     
}


