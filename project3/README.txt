CLAIM: I refer to Beej's guide to network programming during this project.

Usage:
./ping_client --server_ip=<server ip addr> 
              --server_port=<server port>
              --count=<number of pings to send>  
              --period=<wait interval> 
              --timeout=<timeout>

Makefile include, please compile first. Timeout and period are in millisecond.

Instruction:
I use multiple thread to send and receive datagram. Each thread open a socket and send datagram and open a timer via select(). If the thread receive datagram with wrong checksum, the thread keeps listening until the timer expires.

