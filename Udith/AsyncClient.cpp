#include <iostream>
#include <netdb.h>
#include <string.h>
#include <poll.h>
#include "UAP.hpp"
#include <unistd.h>

using namespace std;

#define START 0         // before the client starts up and sends the initial HELLO message
#define WAIT 1          // while waiting for HELLO response from server. When successful moves to READY state
#define READY 2         // once it recieves HELLO it gets into READY state. Here it recieves ALIVE from server for sending DATA. DATA is taken from stdin
#define READY_TIMER 3   // READY state takes input from STDIN and moves to READY_TIMER. When READY_TIMER recieves ALIVE, it moves to READY STATE
#define CLOSING 4       // It gets into CLOSING state if it recieves eof or SIGINT or q or timeout in the input from READY and WAIT states
#define CLOSED 5        // it gets into CLOSED state from CLOSING on timeout or GOODBYE from server

// NOTE: Client transitions to CLOSED state if it recieves GOODBYE no matter which state it is in

#define TIMEOUT 10

#define SERVER_IP   "127.0.0.1"
#define PORT        "8000"

int STATE = START; 
/*
    1: Hello wait / Ready / Ready Timer State
    0: Closing State
*/

time_t START_TIME;
time_t CURR_TIME;
bool TIMER = false; // state to check if the timer is on
bool TIMEOUT_FLAG = false;
int32_t seq_no = -1;
int32_t session_id = random();

void NoPayloadSend(int &sockfd, sockaddr *servaddr, socklen_t &socksize){
    char buffer[RECV_BUFFER_SIZE];
    if (STATE == START){
        seq_no++;
        time(&CURR_TIME);
        MessageFactory::CreateHeader(buffer);
        MessageFactory::setCommand(HELLO, buffer);
        MessageFactory::setTimestamp((int64_t) CURR_TIME, buffer);
        MessageFactory::setSequenceNumber(seq_no, buffer);
        MessageFactory::setSessionId(session_id, buffer);
        printf("%s\n", "sent: HELLO");
        sendto(sockfd, buffer, HEADER_SIZE, 0, servaddr, socksize);
        STATE = WAIT; TIMER = true;
        time(&START_TIME);
    }
    else if (STATE == WAIT){
        // do nothing
    }
}

void PayloadSend(int &sockfd, sockaddr *servaddr, socklen_t &socksize){
    char buffer[RECV_BUFFER_SIZE];
    if (STATE == READY){
        string input;
        getline(cin, input);

        if (cin.eof() || (input.length() == 1 && input[0] == 'q')){
            seq_no++;
            STATE = CLOSING;
            time(&CURR_TIME);
            MessageFactory::CreateHeader(buffer);
            MessageFactory::setCommand(GOODBYE, buffer);
            MessageFactory::setTimestamp((int64_t) CURR_TIME, buffer);
            MessageFactory::setSequenceNumber(seq_no, buffer);
            MessageFactory::setSessionId(session_id, buffer);
            printf("%s %08x %d %s\n", "sent:", session_id, seq_no, "GOODBYE");
            fflush(stdout);
            sendto(sockfd, buffer, HEADER_SIZE, 0, servaddr, socksize);
        }
        else{
            seq_no++;
            size_t sender_payload_size = min(RECV_BUFFER_SIZE-HEADER_SIZE, input.length());
            char *payload = (char *) input.c_str();
            time(&CURR_TIME);
            MessageFactory::CreateHeader(buffer);
            MessageFactory::setCommand(DATA, buffer);
            MessageFactory::setTimestamp((int64_t) CURR_TIME, buffer);
            MessageFactory::setSequenceNumber(seq_no, buffer);
            MessageFactory::setSessionId(session_id, buffer);
            MessageFactory::WriteMessage(buffer, payload, buffer, HEADER_SIZE + sender_payload_size);
            printf("%s %08x %d %s %s\n", "sent:", session_id, seq_no, "DATA", payload);
            fflush(stdout);
            sendto(sockfd, buffer, HEADER_SIZE + sender_payload_size, 0, servaddr, socksize);
            TIMER = true; time(&START_TIME);
            STATE = READY_TIMER;
        }
    }
    else if (STATE == READY_TIMER){
        string input;
        getline(cin, input);
        if (cin.eof() || (input.length() == 1 && input[0] == 'q')){
            seq_no++;
            STATE = CLOSING;
            time(&CURR_TIME);
            MessageFactory::CreateHeader(buffer);
            MessageFactory::setCommand(GOODBYE, buffer);
            MessageFactory::setTimestamp((int64_t) CURR_TIME, buffer);
            MessageFactory::setSequenceNumber(seq_no, buffer);
            MessageFactory::setSessionId(session_id, buffer);
            printf("%s %08x %d %s\n", "sent:", session_id, seq_no, "GOODBYE");
            fflush(stdout);
            sendto(sockfd, buffer, HEADER_SIZE, 0, servaddr, socksize);
        }
        else{
            seq_no++;
            size_t sender_payload_size = min(RECV_BUFFER_SIZE-HEADER_SIZE, input.length());
            char *payload = (char *) input.c_str();
            time(&CURR_TIME);
            MessageFactory::CreateHeader(buffer);
            MessageFactory::setCommand(DATA, buffer);
            MessageFactory::setTimestamp((int64_t) CURR_TIME, buffer);
            MessageFactory::setSequenceNumber(seq_no, buffer);
            MessageFactory::setSessionId(session_id, buffer);
            MessageFactory::WriteMessage(buffer, payload, buffer, HEADER_SIZE + sender_payload_size);
            printf("%s %08x %d %s %s\n", "sent:", session_id, seq_no, "DATA", payload);
            fflush(stdout);
            sendto(sockfd, buffer, HEADER_SIZE + sender_payload_size, 0, servaddr, socksize);
        }
    }
}

void Recieve(int &sockfd, sockaddr *servaddr, socklen_t &socksize){
    char buffer[HEADER_SIZE];
    recvfrom(sockfd, buffer, HEADER_SIZE, 0, servaddr, &socksize);
    uint16_t magic = MessageFactory::getMagic(buffer);
    uint8_t version = MessageFactory::getVersion(buffer);
    uint8_t command = MessageFactory::getCommand(buffer);
    uint32_t recieve_session_id = MessageFactory::getSessionId(buffer);
    uint32_t recieved_seq_no = MessageFactory::getSequenceNumber(buffer);
    if (magic != MAGIC || version != VERSION){
        return;
    }
    if (STATE == WAIT && command == HELLO){
        session_id = recieve_session_id;
        seq_no = 0;
        STATE = READY;
        TIMER = false;
    }
    else if (STATE == READY_TIMER && command == ALIVE && session_id == recieve_session_id){
        STATE = READY;
        TIMER = false;
    }
    else if (command == GOODBYE && session_id == recieve_session_id){
        STATE = CLOSED;
    }
    else if ((STATE == READY || STATE == CLOSING) && command == ALIVE && session_id == recieve_session_id){
        // do nothing
    }
    if (recieve_session_id != session_id){
        return;
    }
    switch (command)
    {
    case HELLO:
        printf("%s %08x %d %s\n", "recieved:", recieve_session_id, recieved_seq_no, "HELLO");
        break;
    case ALIVE:
        printf("%s %08x %d %s\n", "recieved:", recieve_session_id, recieved_seq_no, "ALIVE");
        break;
    case GOODBYE:
        printf("%s %08x %d %s\n", "recieved:", recieve_session_id, recieved_seq_no, "GOODBYE");
        break;
    default:
        printf("%s %s\n", "recieved:", "?");
        break;
    }
    fflush(stdout);
}

int main(int arg, char *argv[]){
    addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_INET;
    char* serverIP = argv[1];
    char* serverPort = argv[2];
    getaddrinfo(serverIP, serverPort, &hints, &res);
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    sockaddr_in servaddr;
    socklen_t socksize = sizeof(servaddr);
    memcpy(&servaddr, res->ai_addr, sizeof(servaddr));
    freeaddrinfo(res);

    /*
        async:
    */
    pollfd fds[2];
    fds[0].fd = STDIN_FILENO; fds[0].events = POLLIN;
    fds[1].fd = sockfd; fds[1].events = POLLIN;

    char buffer[RECV_BUFFER_SIZE];
    MessageFactory::CreateHeader(buffer);
    MessageFactory::setCommand(HELLO, buffer);
    while(STATE != CLOSED){
        int ret = poll(fds, 2, 100);
        if ((STATE == READY || STATE == READY_TIMER) && (fds[0].revents & POLLIN)){
            PayloadSend(sockfd, (sockaddr *) &servaddr, socksize);
        }
        else if (STATE == START || STATE == WAIT){
            NoPayloadSend(sockfd, (sockaddr *) &servaddr, socksize);
        }
        if (fds[1].revents & POLLIN){
            Recieve(sockfd, (sockaddr *) &servaddr, socksize);
        }
        if (TIMER){
            // cerr << "yo\n" << STATE << "\n";
            // timout check is active
            time(&CURR_TIME);
            if (CURR_TIME - START_TIME > TIMEOUT){
                TIMEOUT_FLAG = true;
                cout << (CURR_TIME-START_TIME);
            }
        }
        if (TIMEOUT_FLAG && (STATE == WAIT || STATE == READY_TIMER || STATE == CLOSING)){
            STATE = (STATE == CLOSING) ? CLOSED : CLOSING;
        }
    }
}