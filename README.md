# P2P-chat-
# A peer-to-peer chat for multiple users using threads, signals and protocols

## For the Peer
A peer that is connected to a server using TCP/IP connection and to other peers using UDP connection.
The peer can:
1) write messages to a chat
2) write these messages to a DB
3) edit an entry of the DB
The peer is built using:
- 4 threads: 1 for sending messages to the server and editiing a DB entry, 1 for sending messages to 
other peers, 1 for receiving messages from other peers, 1 for receiving server messages.
- signals for exiting are implemented
- locking of keys for DB entry editing
menu of commands: /help --> /msg (message), /edit (message) - (number of entry you want to edit), /list, /exit
All users must be active since the beggining.
The DB is implemented using local txt files. The connections are made using Stream and Datagram sockets.
Before sending messages to the chat, or editing you have to run /list from the menu.
Protocols used: Total Order Multicast (chat) , 2 PC (edit), consistent global states (server)
Technologies used: Ubuntu 18.04, gcc 7.5, Coded in C
compile: gcc peer.c -o peer -lpthread
Run: ./peer 9000 (...9256) 

## For the server
