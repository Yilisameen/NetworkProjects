CLAIM: I refer to Beej's guide to network programming to impliment this project.

Usage:
./tftp_server <port> <timeout>

(Makefile included, so use make to compile the tftp_server firstly. Timeout is in milliseconds)

Instruction:
1. I fork new process to support multiplee simultaneous file transfer.
2. Then I open new socket(new ephemeral port) in every process for each file transfer.
3. I use select() function as timer, when time out, the server will retransmit data and ACKs.
5. When duplicate data received, the server will retransmit ACK; when duplicate ACK received, the server will keep listening without retransmit data.

