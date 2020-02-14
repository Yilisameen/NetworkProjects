CLAIM: I adapt the code from https://beej.us/guide/bgnet/html/multi/index.html to impliment the project.

Server:
1. Usage: ./server <port>
2. The server will print "Waiting for connection..." once you run it successfully, you can use isof -i:<port> to check if the port is activated.
3. Everytime there is an connection is opened, the server will print "Get connection from <ip_address>.
4.The server will print the message it received via the connection.

Client:
1. Usage: ./cient <localhost> <port>
2. Everytime the client will send a line of your input to the server.
3. After the client receives the message from the server, it will print it.
4. Use ctrl+c to end your input.
