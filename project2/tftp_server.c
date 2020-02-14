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
#pragma comment(lib,"WS2_32")

//define to packet
typedef union{
    uint16_t opcode;//used for server to tell what kind of request it is
    struct{//RRQ AND WRQ
        uint16_t opcode;
        uint8_t filename_and_mode[514];
    }request;
    struct{//DATA
        uint16_t opcode;
        uint16_t block_number;
        uint8_t data[512];
    }data;
    struct{//ACK
        uint16_t opcode;
        uint16_t block_number;
    }ack;
    struct{//ERROR
        uint16_t opcode;
        uint16_t errro_code;
        uint8_t error_msg[512];
    }error;
}datagram;

//Send data to client
ssize_t Data_send(int s, struct sockaddr_in *to, socklen_t tolen, uint16_t block_number, uint8_t *data, ssize_t data_length){//first three parameters used for sendto(), the rest parameters are about the datagram
    datagram message;
    ssize_t res;
    
    message.opcode=htons(3);
    message.data.block_number=htons(block_number);
    memcpy(message.data.data, data, data_length);
    
    res=sendto(s, &message, data_length+4, 0, (struct sockaddr *)to, tolen);
    if(res<0){
        perror("server: sendto()");
    }
    return res;
}

//Send ACK to client
ssize_t ACK_send(int s, struct sockaddr_in *to, socklen_t tolen, uint16_t block_number){
    datagram message;
    ssize_t res;
    
    message.ack.opcode=htons(4);
    message.ack.block_number=htons(block_number);
    
    res=sendto(s, &message, sizeof(message.ack), 0, (struct sockaddr *)to, tolen);
    if(res<0){
        perror("server: sendto()");
    }
    return res;
}

/*Send error to client*/

//Receive data from client
ssize_t Data_receive(int s, struct sockaddr_in *from, socklen_t *fromlen, datagram *message){
    ssize_t res;
    res=recvfrom(s, message, sizeof(*message), 0, (struct sockaddr *) from, fromlen);
    if(res<0&&errno!=EAGAIN){
        perror("server:recvfrom()");
    }
    return res;
}

//Handle datagram received
void Data_handle(datagram *message, ssize_t data_length,struct sockaddr_in *from, socklen_t fromlen, struct timeval timeout){
    int s;//sock number for new request
    struct protoent *protocol=getprotobyname("udp");//current using protocol is udp or not
    char *filename, *mode, *request_end;//filename to read or write, tftp mode, end of the request
    FILE* fd;
    uint16_t opcode;
    
    if(protocol==0){//not using udp protocol
        fprintf(stderr, "server: protocol not udp or getprotobyname() error\n");
        exit(1);
    }
    //open new socket using ephemeral port
    s=socket(AF_INET, SOCK_DGRAM, protocol->p_proto);
    if(s==-1){//open socket fail
        perror("server: socket()");
        exit(1);
    }
    
    //handle the request
    filename=message->request.filename_and_mode;
    request_end=filename+data_length-2-1;
    
    if(*request_end!='\0'){
        printf("Invalid filename or mode...\n");
        exit(1);
    }
    mode=strchr(filename, '\0')+1;//get mode from the request
    if(mode>request_end){
        printf("Transfer mode not specified...\n");
        exit(1);
    }
    if(strcasecmp(mode, "octet")!=0){
        printf("The server supports only binary mode!\n");
        exit(1);
    }
    
    opcode=ntohs(message->request.opcode);
    fd=fopen(filename, opcode==1?"r":"w");
    
    if(fd==NULL){
        perror("server:fopen()");
        //send error to client
        exit(1);
    }
    
    printf("Request received...\n");
    
    //If the request is RRQ
    if(opcode==1){
        /*THE RRQ PROCESS*/
        datagram message;
        uint8_t data_field[512];
        ssize_t data_length,data_count;
        uint16_t block_number=0;
        int end_flag=0;
	struct timeval timer;
        fd_set readfds;
        
        while(!end_flag){
            data_length=fread(data_field, 1, sizeof(data_field), fd);
		timer=timeout;
            block_number++;
            if(data_length<512){
                end_flag=1;
            }
        SEND:data_count=Data_send(s, from, fromlen, block_number, data_field, data_length);
            if(data_count<0){
                printf("Error when sending data...\n");
                exit(1);
            }
            //set data retransmission timer
            FD_ZERO(&readfds);
            FD_SET(s,&readfds);
            
            while(1){
                select(s+1, &readfds, NULL, NULL, &timer);
                if(FD_ISSET(s,&readfds)){//receive data
                    data_count=Data_receive(s, from, &fromlen, &message);
                    if(data_count>=0&&data_count<4){
                        printf("Invalid message size...\n");
                        //continue or exit?
                        exit(1);
                    }
                    else if(data_count>=4){
                        if(ntohs(message.opcode)==5){//receive error
                            printf("ERROR message received! Server exit!\n");
                            exit(1);
                        }
                        if(ntohs(message.opcode)!=4){//receive not acks
                            printf("Invalid message received!\n");
                            exit(1);//exit or still?
                        }
                        if(ntohs(message.opcode)==4){//receive acks
                            if(ntohs(message.ack.block_number)==block_number){//receive right ack
                                break;
                            }
                            else if(ntohs(message.ack.block_number)>block_number){//receive wrong ack
                                printf("Invalide ACK number received!\n");
                                //send error to client
                                exit(1);
                            }
                            else if(ntohs(message.ack.block_number)<block_number){//receive duplicate ack
                                continue;
                            }
                        }
                    }
                }
                else{//time out,resend data
                    goto SEND;
                }
            }
        }
    }
    //If the request is WRQ
    if(opcode==2){
        /*THE WRQ PROCESS*/
        datagram message;
        ssize_t data_count;
        uint16_t block_number=0;
        int end_flag=0;
	struct timeval timer;
        fd_set readfds;
        
        while(!end_flag){
        SEND_ACK:data_count=ACK_send(s, from, fromlen, block_number);
        timer=timeout;    
	if(data_count<0){
                printf("transfer error!\n");
                exit(1);
            }
            //set data retransmission timer
            FD_ZERO(&readfds);
            FD_SET(s,&readfds);
            
            while(1){
                select(s+1, &readfds, NULL, NULL, &timer);
                if(FD_ISSET(s,&readfds)){//receive data
                    data_count=Data_receive(s, from, &fromlen, &message);
                    if(data_count>=0&&data_count<4){
                        printf("Invalid message size...\n");
                        //continue or exit?
                        exit(1);
                    }
                    else if(data_count>=4){
                        block_number++;
                        
                        
                        if(ntohs(message.opcode)==5){//receive ERROR
                            printf("ERROR message received! exit!\n");
                            exit(1);
                        }
                        if(ntohs(message.opcode)!=3){//received not DATA
                            printf("Invalid message received!\n");
                            exit(1);
                        }
                        
                        if(ntohs(message.data.block_number)>block_number){
                            printf("Invalid block number data received!");
                            exit(1);
                        }
                        if(ntohs(message.data.block_number)<block_number){
			    block_number--;
                            goto SEND_ACK;
                        }
                        if(ntohs(message.data.block_number)==block_number)
                            break;
                    }
                }
                else{//time out,resend ack
                    goto SEND_ACK;
                }
            }

            if(data_count<sizeof(message.data))//end of file;
                end_flag=1;
            
            data_count=fwrite(message.data.data, 1, data_count-4, fd);//write file
            
            if(data_count<0){
                perror("server:fwrite()");
                exit(1);
            }
            
            if(end_flag){//end of file
                data_count=ACK_send(s, from, fromlen, block_number);//send ACK to client
                
                if(data_count<0){
                    printf("error with transfer!\n");
                    exit(1);
                }
            }
        }
    }
    
    printf("File transfer completed!\n");
    
    fclose(fd);//close the file
    close(s);//close the socket
    
    exit(0);
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
    char* PORT=argv[1];//port number
    struct timeval TIMEOUT;
    TIMEOUT.tv_usec=1000*atoi(argv[2]);//timeout

    int sockfd;// listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    int rv;
    
    
    if(argc!=3){
        fprintf(stderr, "Format of input should be like: ./tftp_server {port_number} {timeout(in milliseconds)}");
        exit(1);
    }
    
    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_UNSPEC;//set to AP_INET to force IPv4
    hints.ai_socktype=SOCK_DGRAM;//use UDP protocol
    hints.ai_flags=AI_PASSIVE;
    
    if((rv=getaddrinfo(NULL, PORT, &hints, &servinfo))!=0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("sever: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);
    
    printf("server: waiting to recvfrom...\n");
    
    while(1){
        struct sockaddr_in client_sock;
        socklen_t sock_length=sizeof(client_sock);
        ssize_t data_length;
        
        datagram message;
        uint16_t opcode;
        
        data_length=recvfrom(sockfd, &message, sizeof(message), 0, (struct sockaddr *)&client_sock, &sock_length);
        if(data_length<4){
            printf("INVALID SIZE REQUEST\n");
            continue;
        }
        
        opcode=ntohs(message.opcode);
        if(opcode==1||opcode==2){
            //child process
            if(fork()==0){
                Data_handle(&message, data_length, &client_sock, sock_length, TIMEOUT);
                exit(0);
            }
        }
        else{
            printf("INVALID REQUEST!");
            //SEND ERROR TO CLIENT
        }
    }
    
    close(sockfd);
    return 0;
}

