#include "UAP.hpp"
#include <arpa/inet.h>
#include <cstdlib>
#include <iostream>
#include <netdb.h>
#include <pthread.h>
#include <queue>
#include <signal.h>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
using namespace std;

pthread_mutex_t lock;

#define TIMEOUT 60

struct session_state {
    pthread_t thread;
    uint32_t seq_no;
    time_t timer;
    session_state() {
        thread = 0;
        seq_no = 0;
        time( &timer );
    }
    session_state( pthread_t thread, uint32_t seq_no, time_t timer ) {
        this->thread = thread;
        this->seq_no = seq_no;
        this->timer = timer;
    }
};

int sockfd;

unordered_map<pthread_t, queue<sendargs>>
    freelist; // memory blocks that need to be freed on exit
unordered_map<uint32_t, session_state>
    session_map;                               // session : pthread_t, seq_no
unordered_map<pthread_t, uint32_t> thread_map; // pthread_t, session_id

uint32_t session_counter = 1;
uint64_t logicalClock = 0;

// void handle_exit(int SIGNAL){
//     close(sockfd);
//     pthread_mutex_destroy(&lock);
//     cout << "exiting" << "\n";
//     exit(SIGNAL);
// }

void handle_thread_exit( int SIGNAL ) { pthread_exit( NULL ); }

void *ReadSTDIN( void * ) {
    int x;
    while ( 1 ) {
        x = fgetc( stdin );
        if ( x == EOF || (char)x == 'q' ) {
            kill( getpid(), SIGINT );
        }
    }
    pthread_detach( pthread_self() );
    return NULL;
}

void *session_listen( void * ) {
    signal( SIGUSR1, handle_thread_exit );
    pthread_t session_thread = pthread_self();
    sendargs *args;
    bool sendflag = false;
    bool continue_session = true;
    time_t last_time;
    time_t current_time;
    uint8_t command;
    uint32_t session_id, seq_no;
    sendargs temp;
    while ( continue_session ) {
        pthread_mutex_lock( &lock );
        if ( !freelist[session_thread].empty() ) {
            sendargs args = freelist[session_thread].front();
            freelist[session_thread].pop();
            sendflag = true;
            temp = args;
            pthread_mutex_unlock( &lock );
            command = MessageFactory::getCommand( args.buffer );
            session_id = MessageFactory::getSessionId( args.buffer );
            seq_no = MessageFactory::getSequenceNumber( args.buffer );
            if ( command == GOODBYE ) {
                continue_session = false;
                printf( "0x%08x %s\n", session_id, "Session closed" );
            } else if ( command == HELLO ) {
                printf( "0x%08x [%d] %s\n", session_id, seq_no,
                        "Session started" );
            }
            time( &current_time );
            last_time = current_time;
            MessageFactory::setTimestamp( (uint64_t)current_time, args.buffer );
            sendto( args.sockfd, args.buffer, args.buffer_size, args.flags,
                    args.addr, args.sock_len );
            continue;
        } else {
            time( &current_time );
            if ( current_time - last_time > TIMEOUT ) {
                continue_session = false;
            }
        }
        pthread_mutex_unlock( &lock );
    }
    pthread_mutex_lock( &lock );
    if ( thread_map.find( session_thread ) != thread_map.end() ) {
        session_id = thread_map[session_thread];
        printf( "0x%08x %s\n", session_id, "Session closed" );
        temp.buffer_size = HEADER_SIZE;
        MessageFactory::setCommand( GOODBYE, temp.buffer );
        sendto( temp.sockfd, temp.buffer, temp.buffer_size, temp.flags,
                temp.addr, temp.sock_len );
        if ( session_map.find( session_id ) != session_map.end() )
            session_map.erase( session_id );
        while ( !freelist[session_thread].empty() ) {
            auto front = freelist[session_thread].front();
            freelist[session_thread].pop();
        }
        freelist.erase( session_thread );
        thread_map.erase( session_thread );
    }
    pthread_mutex_unlock( &lock );
    pthread_detach( session_thread );
    return NULL;
}

void delete_session( uint32_t session_id, sendargs &args ) {
    // expects to be already locked
    pthread_t session_thread = session_map[session_id].thread;
    session_map.erase( session_id );
    while ( !freelist[session_thread].empty() ) {
        auto front = freelist[session_thread].front();
        freelist[session_thread].pop();
    }
    freelist.erase( session_thread );
    thread_map.erase( session_thread );
    time_t current_time;
    time( &current_time );
    MessageFactory::CreateHeader( args.buffer );
    MessageFactory::setCommand( GOODBYE, args.buffer );
    MessageFactory::setTimestamp( (uint64_t)current_time, args.buffer );
    printf( "0x%08x %s\n", session_id, "Session closed" );
    sendto( args.sockfd, args.buffer, args.buffer_size, args.flags, args.addr,
            args.sock_len );
    pthread_kill( session_thread, SIGUSR1 );
}

void create_session( uint32_t session_id, sendargs &args ) {
    // expects to be already locked
    pthread_t session_thread;
    MessageFactory::setSessionId( session_id, args.buffer );
    MessageFactory::setSequenceNumber( 0, args.buffer );
    session_counter++;
    time_t curr_time;
    time( &curr_time );
    if ( pthread_create( &session_thread, NULL, session_listen, NULL ) == 0 ) {
        session_state state( session_thread, 0, curr_time );
        session_map[session_id] = state;
        thread_map[session_thread] = session_id;
        freelist[session_thread] = queue<sendargs>();
        freelist[session_thread].push( args );
    } else {
        perror( "thread failed to create\n" );
        exit( -1 );
    }
}

void append_session( uint32_t session_id, sendargs args ) {
    // expects to be already locked
    char *buffer = args.buffer;
    ++session_map[session_id].seq_no;
    time( &session_map[session_id].timer );
    MessageFactory::setSessionId( session_id, buffer );
    MessageFactory::setSequenceNumber( session_map[session_id].seq_no, buffer );
    pthread_t session_thread = session_map[session_id].thread;
    freelist[session_thread].push( args );
}

void Response( sendargs &args ) {
    char *input_buffer = args.buffer;
    size_t payload_size = args.buffer_size - HEADER_SIZE;
    char data[payload_size + 1];
    memset( data, 0, payload_size + 1 ); // + 1 for string ending character
    MessageFactory::ReadPayload( input_buffer, data, payload_size );
    if ( MessageFactory::getMagic( input_buffer ) != MAGIC ||
         MessageFactory::getVersion( input_buffer ) != VERSION ) {
        // silently discard the packet as it doesn't match the given
        // requirements
        return;
    }
    uint8_t command = MessageFactory::getCommand( input_buffer );
    uint32_t session_id = MessageFactory::getSessionId( input_buffer );
    uint32_t sequence_no = MessageFactory::getSequenceNumber( input_buffer );
    uint32_t client_timestamp = MessageFactory::getTimestamp( input_buffer );
    args.buffer_size = HEADER_SIZE;

    MessageFactory::CreateHeader( args.buffer );
    time_t curr_time;
    pthread_t session_thread;
    // check timeout in HELLO and DATA messages
    if ( command == HELLO ) {
        time( &curr_time );
        pthread_mutex_lock( &lock );
        if ( session_map.find( session_id ) != session_map.end() ) {
            if ( curr_time - session_map[session_id].timer >= TIMEOUT ) {
                delete_session( session_id, args );
                pthread_mutex_unlock( &lock );
                return;
            }
        }
        pthread_mutex_unlock( &lock );
        MessageFactory::setCommand( HELLO, args.buffer );
        pthread_mutex_lock( &lock );
        if ( session_map.find( session_id ) != session_map.end() ) {
            // received HELLO in recieve state
            delete_session( session_id, args );
        } else {
            create_session( session_counter, args );
        }
        pthread_mutex_unlock( &lock );
    } else if ( command == GOODBYE ) {
        MessageFactory::setCommand( GOODBYE, args.buffer );
        pthread_mutex_lock( &lock );
        if ( session_map.find( session_id ) != session_map.end() ) {
            append_session( session_id, args );
            session_map.erase( session_id );
        }
        pthread_mutex_unlock( &lock );
    } else if ( command == DATA ) {
        time( &curr_time );
        pthread_mutex_lock( &lock );
        if ( session_map.find( session_id ) != session_map.end() ) {
            if ( curr_time - session_map[session_id].timer >= TIMEOUT ) {
                delete_session( session_id, args );
                pthread_mutex_unlock( &lock );
                return;
            }
        }
        pthread_mutex_unlock( &lock );
        pthread_mutex_lock( &lock );
        if ( session_map.find( session_id ) == session_map.end() ) {
            // Do nothing as it is new session but sending DATA instead of HELLO
        } else if ( session_map[session_id].seq_no == sequence_no ) {
            printf( "0x%08x [%d] %s\n", session_id, sequence_no,
                    "duplicate packet" );
        } else if ( session_map[session_id].seq_no + 1 < sequence_no ) {
            for ( auto i = session_map[session_id].seq_no; i < sequence_no;
                  i++ )
                printf( "0x%08x [%d] %s\n", session_id, i, "lost packet\n" );
            printf( "0x%08x [%d] %s\n", session_id, sequence_no, data );
        } else if ( session_map[session_id].seq_no > sequence_no ) {
            MessageFactory::setCommand( GOODBYE, args.buffer );
            append_session( session_id, args );
            session_map.erase(
                session_id ); // no more messages to this session again
        } else {
            // when session_seq[session] + 1 == sequence_no
            MessageFactory::setCommand( ALIVE, args.buffer );
            append_session( session_id, args );
            printf( "0x%08x [%d] %s\n", session_id, sequence_no, data );
        }
        pthread_mutex_unlock( &lock );
    }
}

int main( int argc, char *argv[] ) {
    pthread_mutex_init( &lock, NULL );
    struct addrinfo hints, *servinfo;
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    if ( getaddrinfo( NULL, argv[1], &hints, &servinfo ) != 0 ) {
        perror( "not getting address info\n" );
        exit( -1 );
    }
    sockfd = socket( servinfo->ai_family, servinfo->ai_socktype,
                     servinfo->ai_protocol );
    if ( sockfd == -1 ) {
        perror( "unable to create socket\n" );
        exit( -2 );
    }
    if ( bind( sockfd, servinfo->ai_addr, servinfo->ai_addrlen ) == -1 ) {
        perror( "unable to bind to socket\n" );
        exit( -3 );
    };
    char host[NI_MAXHOST], port[NI_MAXSERV];
    getnameinfo( servinfo->ai_addr, (socklen_t)servinfo->ai_addrlen, host,
                 NI_MAXHOST, port, NI_MAXSERV, 0 );
    freeaddrinfo( servinfo );
    cout << "server started at: " << host << ":" << port << "\n";

    // signal(SIGINT, handle_exit);
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof( client_addr );
    char buffer[RECV_BUFFER_SIZE];
    pthread_t STDIN_reader;
    if ( pthread_create( &STDIN_reader, NULL, ReadSTDIN, NULL ) != 0 ) {
        kill( getpid(), SIGINT );
    }
    while ( 1 ) {
        int PACKET_SIZE = recvfrom( sockfd, buffer, RECV_BUFFER_SIZE, 0,
                                    (sockaddr *)&client_addr, &addr_len );
        if ( PACKET_SIZE == -1 ) {
            continue;
        }
        sendargs args( sockfd, buffer, PACKET_SIZE, 0, &client_addr, addr_len );
        Response( args );
    }
    return 0;
}
