////////////////////////////////////////////////////////////////////////////////
//
//  File          : cart_client.c
//  Description   : This is the client side of the CART communication protocol.
//
//   Author       : ????
//  Last Modified : ????
//

// Include Files
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
// Project Include Files
#include <cart_network.h>
#include <cmpsc311_log.h>
//#include <cart_driver.h>
//
//  Global data
int 		   client_socket = -1;
int                cart_network_shutdown = 0;   // Flag indicating shutdown
unsigned char     *cart_network_address = NULL; // Address of CART server
unsigned short     cart_network_port = 0;       // Port of CART serve
unsigned long      CartControllerLLevel = LOG_INFO_LEVEL; // Controller log level (global)
unsigned long      CartDriverLLevel = 0;     // Driver log level (global)
unsigned long      CartSimulatorLLevel = 0;  // Driver log level (global)
//
// Functions

struct {
	uint8_t kyOne;
	uint16_t ctOne;
	uint16_t fmOne;
	uint8_t rt;
} regstate;

void temp_readBusResponse(uint64_t busResponse) { 	
	uint64_t temp;
	
	temp = busResponse & 0xff00000000000000; // Reads key register one
	regstate.kyOne = temp >> 56;

	temp = busResponse & 0x7fff80000000; // Reads cartridge register one
	regstate.ctOne = temp >> 31;
	
	temp = busResponse & 0x7fff8000; // Reads frame register one
	regstate.fmOne = temp >> 15;
	
	temp = busResponse & 0x800000000000; // Reads return code
	regstate.rt = temp >> 47;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : client_cart_bus_request
// Description  : This the client operation that sends a request to the CART
//                server process.   It will:
//
//                1) if INIT make a connection to the server
//                2) send any request to the server, returning results
//                3) if CLOSE, will close the connection
//
// Inputs       : reg - the request reqisters for the command
//                buf - the block to be read/written from (READ/WRITE)
// Outputs      : the response structure encoded as needed

CartXferRegister client_cart_bus_request(CartXferRegister reg, void *buf) {
	struct sockaddr_in caddr; 
	CartXferRegister registerValue;
	char bufValue[CART_FRAME_SIZE];	

	if(client_socket == -1) {
		// Set the address
		caddr.sin_family = AF_INET;
		caddr.sin_port = htons(CART_DEFAULT_PORT);
		if(inet_aton(CART_DEFAULT_IP, &caddr.sin_addr) == 0) {
			printf("Error setting the address\n");
			return -1;
		}

		// Create the socket
		client_socket = socket(PF_INET, SOCK_STREAM, 0);
		if(client_socket == -1) {
			printf("Error on socket creation\n");
			// printf("Error on socket creation [%s]\n", strerror(errno));
			return -1;
		}
	
		// Create the Connection
		if(connect(client_socket, (const struct sockaddr *) &caddr, sizeof(caddr)) == -1) {
			printf("Error on socket connect\n");
			// printf("Error on socket connect [%s]\n", strerror(errno));
			return -1;
		}
	}

	// printf("reg = %lu\n", reg);
	temp_readBusResponse(reg);
	// printf("kyOne = %d ctOne = %d fmOne = %d rt = %d\n", regstate.kyOne, regstate.ctOne, regstate.fmOne, regstate.rt);
	registerValue = htonll64(reg);	
	// bufValue = malloc(sizeof(char) * CART_FRAME_SIZE);

	// RD Operation
	if(regstate.kyOne == 3) {
		// printf("RD Operation\n");

		// Network Format: Send the register reg to the network after converting the register to 'network format'
		if(write(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error writing network data\n");
			// printf("Error writing network data [%s]\n", strerror(errno));
			return -1;
		}

		// Host Format: Receive the register reg from the network.  Need to convert the register to 'host format'
		if(read(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error reading network data\n");
			// printf("Error reading network data [%s]\n", strerror(errno));
			return -1;
		}

		// Data read from that frame
		if(read(client_socket, &bufValue, CART_FRAME_SIZE) != CART_FRAME_SIZE) {
			printf("Error reading network data\n");
			// printf("Error reading network data [%s]\n", strerror(errno));
			return -1;
		}

		memcpy(buf, bufValue, CART_FRAME_SIZE);
	}

	// WR Operation
	else if(regstate.kyOne == 4) {
		// printf("WR Operation\n");

		// Network Format: Send the register reg to the network after converting the register to 'network format'
		if(write(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error writing network data\n");
			// printf("Error writing network data [%s]\n", strerror(errno));
			return -1;
		}

		// Data to write to that frame
		if(write(client_socket, &buf, CART_FRAME_SIZE) != CART_FRAME_SIZE) {
			printf("Error writing network data\n");
			// printf("Error writing network data [%s]\n", strerror(errno));
			return -1;
		}

		// Host Format: Receive the register reg from the network.  Need to convert the register to 'host format'
		if(read(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error reading network data\n");
			// printf("Error reading network data [%s]\n", strerror(errno));
			return -1;
		}
	}

	// SHUTDOWN Operation
	else if(regstate.kyOne == 5) {
		// printf("SHUTDOWN Operation\n");

		// Network Format: Send the register reg to the network after converting the register to 'network format'
		if(write(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error writing network data\n");
			// printf("Error writing network data [%s]\n", strerror(errno));
			return -1;
		}

		// Host Format: Receive the register reg from the network.  Need to convert the register to 'host format'
		if(read(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error reading network data\n");
			// printf("Error reading network data [%s]\n", strerror(errno));
			return -1;
		}

		close(client_socket);	
		client_socket = -1;
	}

	// Other
	else {
		// printf("Default Operation\n");

		// Network Format: Send the register reg to the network after converting the register to 'network format'
		if(write(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error writing network data\n");
			// printf("Error writing network data [%s]\n", strerror(errno));
			return -1;
		}

		// Host Format: Receive the register reg from the network.  Need to convert the register to 'host format'
		if(read(client_socket, &registerValue, sizeof(registerValue)) != sizeof(registerValue)) {
			printf("Error reading network data\n");
			// printf("Error reading network data [%s]\n", strerror(errno));
			return -1;
		}
	}
	
	// free(bufValue);

	return ntohll64(registerValue);
}
