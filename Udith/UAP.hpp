#include <cstring>
#include <inttypes.h>
#include <netdb.h>
#include <string>
#include <vector>

using namespace std;

#define HELLO uint8_t( 0 )
#define DATA uint8_t( 1 )
#define ALIVE uint8_t( 2 )
#define GOODBYE uint8_t( 3 )

#define RECV_BUFFER_SIZE size_t( 1024 )
#define HEADER_SIZE size_t( 28 )

#define MAGIC uint16_t( 0xC461 )
#define VERSION uint8_t( 1 )

uint64_t htonll( uint64_t hostbyte ) {
    // reference: https://stackoverflow.com/questions/809902/64-bit-ntohl-in-c
    if ( __BYTE_ORDER == __LITTLE_ENDIAN ) {
        return ( ( (uint64_t)htonl( hostbyte & 0xFFFFFFFFULL ) ) << 32 ) |
               htonl( hostbyte >> 32 );
    } else {
        return hostbyte;
    }
}

uint64_t ntohll( uint64_t netbyte ) {
    if ( __BYTE_ORDER == __LITTLE_ENDIAN ) {
        return ( ( (uint64_t)ntohl( netbyte & 0xFFFFFFFFULL ) ) << 32 ) |
               ntohl( netbyte >> 32 );
    } else {
        return netbyte;
    }
}

// size of header is 28 bytes
/*
item                    size    offset
magic                   2       0
version                 1       2
command                 1       3
sequence number         4       4
session id              4       8
logical clock           8       12
timestamp               8       20
*/

class MessageFactory {
    // used to parse the buffer
  public:
    static uint16_t getMagic( char *buffer ) {
        return ntohs( *(uint16_t *)( buffer ) );
    }
    static uint8_t getVersion( char *buffer ) {
        return *(uint8_t *)( buffer + 2 );
    }
    static uint8_t getCommand( char *buffer ) {
        return *(uint8_t *)( buffer + 3 );
    }
    static uint32_t getSequenceNumber( char *buffer ) {
        return ntohl( *(uint32_t *)( buffer + 4 ) );
    }
    static uint32_t getSessionId( char *buffer ) {
        return ntohl( *(uint32_t *)( buffer + 8 ) );
    }
    static uint64_t getLogicalClock( char *buffer ) {
        return ntohll( *(uint64_t *)( buffer + 12 ) );
    }
    static uint64_t getTimestamp( char *buffer ) {
        return ntohll( *(uint64_t *)( buffer + 20 ) );
    }

    static void setMagic( uint16_t magic, char *buffer ) {
        *(uint16_t *)( buffer ) = htons( magic );
    }
    static void setVersion( uint8_t version, char *buffer ) {
        *(uint8_t *)( buffer + 2 ) = version;
    }
    static void setCommand( uint8_t command, char *buffer ) {
        *(uint8_t *)( buffer + 3 ) = command;
    }
    static void setSequenceNumber( uint32_t seqNumber, char *buffer ) {
        *(uint32_t *)( buffer + 4 ) = htonl( seqNumber );
    }
    static void setSessionId( uint32_t sessionId, char *buffer ) {
        *(uint32_t *)( buffer + 8 ) = htonl( sessionId );
    }
    static void setLogicalClock( uint64_t logicalClock, char *buffer ) {
        *(uint64_t *)( buffer + 12 ) = htonll( logicalClock );
    }
    static void setTimestamp( uint64_t timeStamp, char *buffer ) {
        *(uint64_t *)( buffer + 20 ) = htonll( timeStamp );
    }

    static void CreateHeader( char *buffer ) {
        setMagic( MAGIC, buffer );
        setVersion( VERSION, buffer );
    }
    static void CopyHeader( char *input_buffer, char *output_buffer ) {
        memcpy( output_buffer, input_buffer, HEADER_SIZE );
    }
    static void CreateHeader( int8_t command, int32_t sequence_number,
                              int32_t session_id, int64_t logical_clock,
                              int64_t timestamp, char *buffer ) {
        setCommand( command, buffer );
        setSequenceNumber( sequence_number, buffer );
        setSessionId( session_id, buffer );
        setLogicalClock( logical_clock, buffer );
        setTimestamp( timestamp, buffer );
    }

    static void WriteMessage( char *payload, char *buffer, size_t buffer_size,
                              int8_t command, int32_t seqNum, int32_t sessionId,
                              int64_t logicalClock, int64_t timestamp ) {
        setMagic( MAGIC, buffer );
        setVersion( VERSION, buffer );
        setCommand( command, buffer );
        setSequenceNumber( seqNum, buffer );
        setLogicalClock( logicalClock, buffer );
        setTimestamp( timestamp, buffer );
        memcpy( buffer + HEADER_SIZE, payload, buffer_size - HEADER_SIZE );
    }
    static void WriteMessage( char *header, char *payload, char *output_buffer,
                              size_t buffer_size ) {
        memcpy( output_buffer, header, HEADER_SIZE );
        memcpy( output_buffer + HEADER_SIZE, payload,
                buffer_size - HEADER_SIZE );
    }
    static void ReadHeader( char *buffer, char *header ) {
        memcpy( header, buffer, HEADER_SIZE );
    }
    static void ReadPayload( char *input_buffer, char *output_buffer,
                             size_t n ) {
        memcpy( output_buffer, input_buffer + HEADER_SIZE, n );
    }
};

struct sendargs {
    int sockfd;
    char buffer[RECV_BUFFER_SIZE];
    size_t buffer_size;
    int flags;
    sockaddr *addr;
    socklen_t sock_len;
    sendargs() {
        sockfd = 0;
        buffer_size = 0;
        flags = 0;
        addr = nullptr;
        sock_len = 0;
    }
    sendargs( int sockfd, char *buffer, size_t buffer_size, int flags,
              sockaddr_in *addr, socklen_t sock_len ) {
        this->sockfd = sockfd;
        memcpy( this->buffer, buffer, buffer_size );
        this->buffer_size = buffer_size;
        this->flags = flags;
        this->addr = (sockaddr *)addr;
        this->sock_len = sock_len;
    }
};
