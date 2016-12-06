////////////////////////////////////////////////////////////////////////////////
//
//  File           : cart_driver.c
//  Description    : This is the implementation of the standardized IO functions
//                   for used to access the CART storage system.
//
//  Author         : James W. Frazier
//  Last Modified  : Wednesday, November 23rd
//

// Includes
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Project Includes
#include <cart_driver.h>
#include <cart_controller.h>
#include <cart_cache.h>
//
// Implementation

////////////////////////////////////////////////////////////////////////////////
//
// Structure    : regstate
// Description  : A structure for the packed registers. Variable "regstate" will be updated each time 
//                a bus request is called.

struct {
	uint8_t kyOne;
	uint16_t ctOne;
	uint16_t fmOne;
	uint8_t rt;
} regstate;

////////////////////////////////////////////////////////////////////////////////
//
// Structure    : files
// Description  : A structure for my filysystem. The number of these that are 
//		  created is determined by the number of files that are opened.

typedef struct files {
	char* fileName;
	int32_t length;
	int32_t filePointer;
	struct location {
		int cartridges; // Keeps track of number of cartridges that are occupied
		int* occupiedCartridges; // Number of the cartridges occupied
		int frames; // Keeps track of number of frames that are occupied
		int* occupiedFrames; // Number of the frames occupied
	} location;
	int16_t fileHandle;
} files;

files *filesystem; // Pointer to the filesystem. It will be alloc when the first file is opened, and expaneded as new files are openeded.
int fileSystemSize = 0; // Keeps track of the number of files occupying the filesystem.  0 means 1 file exist, 1 mean means 2 files exist, and so on.
int currentlyLoadedCartridge; // Global int for the cartridge that is currently loaded
int nextFrame = 0; // Number of the next empty frame to write to
int nextCartridge = 0; // Number of the next cartridge with empty frames

////////////////////////////////////////////////////////////////////////////////
//
// Function     : generateBusRequest
// Description  : This function generates the registers need to communicate with the HRAM bus.
//
// Inputs       : busResponse - an uint64_t that is used to read each register
// Outputs      : unint64_t - sent to the client_cart_bus_request

uint64_t generateBusRequest() {
	uint64_t toReturn = 0; // Zero out 64-bit unsigned int. Return code zeroed out.
	uint64_t temp = 0;

	temp = regstate.kyOne; // Key register one. No key register two for this project.
	temp = temp << 56;
	toReturn = temp;

	temp = regstate.ctOne; // Cartridge register one.
	temp = temp << 31;
	toReturn = toReturn + temp;

	temp = regstate.fmOne; // Frame register one
	temp = temp << 15;
	toReturn = toReturn + temp;

	return toReturn;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : readBusResponse
// Description  : Reads the return 64-bit unsigned int from the bus request. Places results in regstate.
//
// Inputs       : busResponse - an uint64_t that is used to read each register
// Outputs      : none

void readBusResponse(uint64_t busResponse) { 	
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
// Function     : runBusRequest
// Description  : Runs a bus request using the parameters passed to it.
//
// Inputs       : kyOne - int for key register one
//		: ctOne - int for cartridge register one
//		: fmOne - int for frame register one
//		: buf - Buffur for reading or writing
// Outputs      : none

void runBusRequest(uint8_t kyOne, uint16_t ctOne, uint16_t fmOne, void *buf) { 	
	regstate.kyOne = kyOne; // Assigns key register one
	regstate.ctOne = ctOne; // Assigns cartridge register one
	regstate.fmOne = fmOne; // Assigns frame register one
	regstate.rt = 0; 

	readBusResponse(client_cart_bus_request(generateBusRequest(), buf)); // Call readBusResponse to read the returned 64-bit unsigned int.
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_poweron
// Description  : Startup up the CART interface, initialize filesystem
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t cart_poweron(void) {
	runBusRequest(0, 0, 0, NULL); // Bus request to initialize memory system.
	if(regstate.rt != 0) { // Returns -1 and prints error if the memory system cannot be initialized.
		printf("cart_poweron: Error initializing the memory system\n");
		return -1;
	}
	for(int i = 0;  i < CART_MAX_CARTRIDGES; i++) {
		runBusRequest(2, i, 0, NULL); // Loads cartridge i.
		if(regstate.rt != 0) { // Returns -1 and prints error if it cannot load cartridge i.
			printf("cart_poweron: Error loading cartridge %d\n", i);
			return -1;
		}
		
		runBusRequest(1, 0, 0, NULL); // Zeros cartridge i.
		if(regstate.rt != 0) { // Returns -1 and prints error if it cannot zero cartridge i.
			printf("cart_poweron: Error zeroing currently loaded cartridge %d\n", i);
			return -1;
		}
		currentlyLoadedCartridge = i;
	}
	if(init_cart_cache() != 0) {
		printf("cart_poweron: Error initializing cache\n");
		return -1;
	}
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_poweroff
// Description  : Shut down the CART interface, close all files
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t cart_poweroff(void) {
	int i;
	for(i=0; i <= fileSystemSize; i++) { // Goes through each file in the filesystem, and frees all the alloced memory
		filesystem[i].fileHandle = 0; 
		free(filesystem[i].fileName); 
		free(filesystem[i].location.occupiedFrames);
		free(filesystem[i].location.occupiedCartridges);
	}

	free(filesystem); // free the whole filesystem itself
	runBusRequest(5, 0, 0, NULL); // Bus request to turn off memory system.
	if(regstate.rt != 0) { // Returns -1 and prints error if it cannot turn off the memory system.
		printf("cart_poweroff: Failed to shutdown filesystem\n");
		return -1;
	}	
	
	if(close_cart_cache() != 0) {
		printf("cart_poweroff: Error shutting down cache\n");
		return -1;
	}
	return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_open
// Description  : This function opens the file and returns a file handle
//
// Inputs       : path - filename of the file to open
// Outputs      : file handle if successful, -1 if failure

int16_t cart_open(char *path) {
	int fileHandleAssign = 0;
	int i, j;
	char fileHandleAvailable;
	files *rfilesystem; // used to see if a pointer initialized by a malloc is null

	// If no file has been open yet in our filesystem, malloc the filesystem, and return the first filehandle
	if(filesystem == NULL) {
		filesystem = (files *) malloc(sizeof(struct files)); // Sets filesystem pointer to size big enough for one files struct. This will get progressively bigger 
								     // in the next project as new files are created.
		filesystem[0].fileName = malloc(sizeof(char) * strlen(path));
		if(filesystem[0].fileName == NULL) { // returns -1 is error with malloc
			printf("cart_open: Error allocating filesystem.filename 0\n");
			return -1;
		}
		
 		filesystem[0].location.occupiedFrames = malloc(sizeof(int));
		if(filesystem[0].location.occupiedFrames == NULL) { // returns -1 is error with malloc
			printf("cart_open: Error allocating filesystem.occupiedFrames 0\n");
			return -1;
		}
	
		filesystem[0].location.occupiedCartridges = malloc(sizeof(int));
		if(filesystem[0].location.occupiedCartridges == NULL) { // returns -1 is error with malloc
			printf("cart_open: Error allocating filesystem.occupiedCartridges 0\n");
			return -1;
		}
		strncpy(filesystem[0].fileName, path, strlen(path)); // Copys String from path to the filename in the filesystem		
		filesystem[0].length = 0; // set length to zero
		filesystem[0].filePointer = 0; // sets filepointer to zero
		filesystem[0].fileHandle = 1; // sets filehandle to one. A file in my system is open if the filehandle > 0
		return 1;
	}
	else { 
		for(i = 0; i <= fileSystemSize; i++) { // looks if a file with the filename path already exists
			if(strcmp(path, filesystem[i].fileName) == 0) { 
				if(filesystem[i].fileHandle > 0) { // if path does exist and is open, return -1 (again, files in my system with filehandles > 0 are considered open
					return -1;
				}
				else {
					do { // if path does exist and is not open, looks for filehandle that is not already assigned.
						fileHandleAssign += 1;
						fileHandleAvailable = 't';	
						for(j = 0; j <= fileSystemSize; j++) {
							if(filesystem[j].fileHandle == fileHandleAssign) {
								fileHandleAvailable = 'f';
							}
						}
					} while (fileHandleAvailable == 'f');
					filesystem[i].filePointer = 0; // sets filepointer to zero
					// Set file's fileHandle to fileHandleAssign
					filesystem[fileSystemSize].fileHandle = fileHandleAssign;
					// Returns successful with file's fileHandlea
					return filesystem[fileSystemSize].fileHandle;
				}	
			}
		}
		
		// if file with filename path doesn't exist, create it
		fileSystemSize++;
		rfilesystem = realloc(filesystem, sizeof(struct files) * (fileSystemSize + 1)); // reallocs filesystem to add another file
		if(rfilesystem == NULL) { // returns -1 is error with malloc
			printf("cart_open: Error reallocating filesystem\n");
			return -1;
		}
		filesystem = rfilesystem;
		filesystem[fileSystemSize].fileName = malloc(strlen(path));
		if(filesystem[fileSystemSize].fileName == NULL) { // returns -1 is error with malloc
			printf("cart_open: Error allocating filesystem.filename %d\n", fileSystemSize);
			return -1;
		}
		filesystem[fileSystemSize].location.occupiedFrames = malloc(sizeof(int));
		if(filesystem[fileSystemSize].location.occupiedFrames == NULL) { // returns -1 is error with malloc
			printf("cart_open: Error allocating filesystem.filename %d\n", fileSystemSize);
			return -1;
		}
		filesystem[fileSystemSize].location.occupiedCartridges = malloc(sizeof(int));
		if(filesystem[fileSystemSize].location.occupiedCartridges == NULL) { // returns -1 is error with malloc
			printf("cart_open: Error allocating filesystem.filename %d\n", fileSystemSize);
			return -1;
		}
		strncpy(filesystem[fileSystemSize].fileName, path, strlen(path)); // copys path to filename
		filesystem[fileSystemSize].length = 0; // sets length to zero
		filesystem[fileSystemSize].filePointer = 0; // sets filepointer to zero
		do { // looks for filehandle that is not already assigned
			fileHandleAssign += 1;
			fileHandleAvailable = 't';
			for(i = 0; i < fileSystemSize; i++) {
				if(filesystem[i].fileHandle == fileHandleAssign) {
					fileHandleAvailable = 'f';	
				}
			}
		} while (fileHandleAvailable == 'f');
		// Set file's fileHandle to fileHandleAssign
		filesystem[fileSystemSize].fileHandle = fileHandleAssign;
		// Returns successful with file's fileHandle
		return filesystem[fileSystemSize].fileHandle;
	}

	// THIS SHOULD RETURN A FILE HANDLE
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_close
// Description  : This function closes the file
//
// Inputs       : fd - the file descriptor
// Outputs      : 0 if successful, -1 if failure

int16_t cart_close(int16_t fd) {
	int fileSystemIndex = -1; // fileSystemIndex is used in the filesystem array to determine which file fd is referring to
				  // ultimately in this project it will be one
	int i;
	for(i=0; i <= fileSystemSize; i++) { // determines fileSystemIndex by looking for the fd in the filesystem array
		if(filesystem[i].fileHandle == fd) {
			fileSystemIndex = i;
			break;
		}
	}
	if(fileSystemIndex == -1) { // returns -1 if the filehandle is bad
		printf("cart_close: filehandle %d is bad\n", fd);
		return -1;
	}
	else if(filesystem[fileSystemIndex].fileHandle == 0) { // returns -1 if the filehandle is not open. A filehandle that equals zero in my filesystem means it is closed
		printf("cart_close: filehandle %d is not open\n", fd);
		return -1;
	}
	filesystem[fileSystemIndex].fileHandle = 0; // sets filehandle to zero (meaning it is closed)
	filesystem[fileSystemIndex].filePointer = 0; // sets pointer to zero

	// Return successfully
	return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_read
// Description  : Reads "count" bytes from the file handle "fh" into the 
//                buffer "buf"
//
// Inputs       : fd - filename of the file to read from
//                buf - pointer to buffer to read into
//                count - number of bytes to read
// Outputs      : bytes read if successful, -1 if failure

int32_t cart_read(int16_t fd, void *buf, int32_t count) {
	char* localBuf; // the local buffer.  Its size is alloced later in this function based on the number of frames that need to be read
	void* get_cart_cache_results; // pointer to determine if the frame exists in the cache.  If it does, it is used to memcpy the frame over to the localBuf
	int fileSystemIndex = -1; // fileSystemIndex is used in the filesystem array to determine which file fd is referring to
	int i; // previousFrame will be compared to the currentFrame to determine if the next cartridge should be loaded 
	int startFrameIndex, endFrameIndex; // used to determine which frames should be loaded

	for(i=0; i <= fileSystemSize; i++) { // determines fileSystemIndex by looking for the fd in the filesystem array
		if(filesystem[i].fileHandle == fd) { 
			fileSystemIndex = i;
			break;
		} 
	}
	if(fileSystemIndex == -1) { // returns -1 if the filehandle is bad
		printf("cart_read: filehandle %d is bad\n", fd);
		return -1;
	}
	else if(filesystem[fileSystemIndex].fileHandle == 0) { // returns -1 if the filehandle is not open. A filehandle that equals zero in my filesystem means it is closed
		printf("cart_read: filehandle %d is not open\n", fd);
		return -1;
	}
	
	// In assign2, I was reading each frame the file was occupying.  This was very inefficient and costly, especially since in assign3 the file size can be as large as desired.
	// So to solve this, I am doing some quick math with the file's filePointer and CART_FRAME SIZE to determine which frames are actually needed.
	startFrameIndex = filesystem[fileSystemIndex].filePointer / CART_FRAME_SIZE;
	endFrameIndex = (filesystem[fileSystemIndex].filePointer + count) / CART_FRAME_SIZE;

	// Since we know how many frames we need to load, we can make the localBuf significantly smaller, and thus saving memory in the heap
	localBuf = malloc(sizeof(char) * CART_FRAME_SIZE * ((endFrameIndex - startFrameIndex) + 1));
	
	
	// We load the frame(s) within this for's scope.  To ensure we do not accidently step outside the filesystem.location.occupiedFrames array,
	// I check the size of filesystem.location.frames (the int that keeps track of the size of filesystem.location.occupiedFrames) too.
	for(i=0; i<=(endFrameIndex - startFrameIndex) && i<=filesystem[fileSystemIndex].location.frames; i++) {
		// Check if the frame is located in the cache.  If it is not, fetch the frame from the bus.
		get_cart_cache_results = get_cart_cache(filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex]);	
		if(get_cart_cache_results == NULL) {	
			// Load cartridge file is located in if it isn't already loaded 
			if(currentlyLoadedCartridge != filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex]) {
				runBusRequest(2, filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], 0, NULL);
				if(regstate.rt != 0) {
					printf("cart_read: Error loading cartridge %d\n", filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex]);
					return -1;
				}
				currentlyLoadedCartridge = filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex];
			}
			runBusRequest(3, 0, filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex], &localBuf[CART_FRAME_SIZE * i]);
			if(regstate.rt != 0) { // Returns -1 if the frame cannot be read
				printf("cart_read: failed to read cartridge %d frame %d\n", filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex]);
				return -1;
			}
		}
		else {
			// Copy void frame from cache to the local buf
			memcpy(&localBuf[CART_FRAME_SIZE * i], get_cart_cache_results, CART_FRAME_SIZE);
		}
	}	

	// If the length of the file < (filePointer + count), only read the remains bytes of the file, and set the filePointer equal to the file's length
	if((filesystem[fileSystemIndex].filePointer + count) > filesystem[fileSystemIndex].length) {
		i = filesystem[fileSystemIndex].length - filesystem[fileSystemIndex].filePointer;
		strncpy(buf, &localBuf[filesystem[fileSystemIndex].filePointer - (startFrameIndex * CART_FRAME_SIZE)], i);
		filesystem[fileSystemIndex].filePointer = filesystem[fileSystemIndex].length;
		// Return successfully with i bytes read
		free(localBuf);
		return i;
	}

	// Copy count characters from the localBuf starting at the filePointer into buf
	strncpy(buf, &localBuf[filesystem[fileSystemIndex].filePointer - (startFrameIndex * CART_FRAME_SIZE)], count); 
	// Update filePointer
	filesystem[fileSystemIndex].filePointer += count;
	// Return successfully with count bytes read
	free(localBuf);
	return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_write
// Description  : Writes "count" bytes to the file handle "fh" from the 
//                buffer  "buf"
//
// Inputs       : fd - filename of the file to write to
//                buf - pointer to buffer to write from
//                count - number of bytes to write
// Outputs      : bytes written if successful, -1 if failure

int32_t cart_write(int16_t fd, void *buf, int32_t count) {
	char* localBuf; // the local buffer.  Its size is alloced later in this function based on the number of frames that need to be read
	int fileSystemIndex = -1; // fileSystemIndex is used in the filesystem array to determine which file fd is referring to
	int i;
	int startFrameIndex, endFrameIndex; // used to determine which frames should be loaded
	char sizeOfFrameBuf[CART_FRAME_SIZE];
	int *myRalloc; // used to see if a pointer initialized by a malloc is null
	void* get_cart_cache_results; // pointer to determine if the frame exists in the cache.  If it does, it is used to memcpy the frame over to the localBuf
	
	for(i=0; i <= fileSystemSize; i++) { // determines fileSystemIndex by looking for the fd in the filesystem array
		if(filesystem[i].fileHandle == fd) { 
			fileSystemIndex = i;
			break;
		} 
	}
	if(fileSystemIndex == -1) { // returns -1 if the filehandle is bad
		printf("cart_write: filehandle %d is bad\n", fd);
		return -1;
	}
	else if(filesystem[fileSystemIndex].fileHandle == 0) { // returns -1 if the filehandle is not open. A filehandle that equals zero in my filesystem means it is closed
		printf("cart_write: filehandle %d is not open\n", fd);
		return -1;
	}

	// Code used for writing to the file's first frame
	if(filesystem[fileSystemIndex].length == 0) {
		filesystem[fileSystemIndex].location.frames = 0; // The number of frames this file occupies.  0 means 1 frame, 1 means 2 frames, and so on...
		filesystem[fileSystemIndex].location.cartridges = 0; // The number of cartridges this file occupies.  0 means 1 frame, 1 means 2 frames, and so on...
		filesystem[fileSystemIndex].location.occupiedFrames[0] = nextFrame; // The int array that keeps track of what specific frames this file is in.
		filesystem[fileSystemIndex].location.occupiedCartridges[0] = nextCartridge; // The int array that keeps track of what specific cartridges this file is in
		nextFrame++;
		if(nextFrame == CART_FRAME_SIZE) { // If the nextFrame is out of the scope of the current cartridge, load the next cartridge, and set the frame back to zero.
			nextFrame = 0;
			nextCartridge++;
		}
		filesystem[fileSystemIndex].length = count; // Set file's length to count 
		filesystem[fileSystemIndex].filePointer = count; // Set file's filePointer to count
		// Updates the sizeOfFrameBuf with count characters from buf
		strncpy(sizeOfFrameBuf, buf, count); 
		
		// Load the cartridge this file occupies
		if(currentlyLoadedCartridge != filesystem[fileSystemIndex].location.occupiedCartridges[0]) {
			runBusRequest(2, filesystem[fileSystemIndex].location.occupiedCartridges[0], 0, NULL);
			if(regstate.rt != 0) {
				printf("cart_write: Error loading cartridge %d\n", filesystem[fileSystemIndex].location.cartridges);
				return -1;
			}
			currentlyLoadedCartridge = filesystem[fileSystemIndex].location.occupiedCartridges[0];
		}
		// Place the frame into the cache
		put_cart_cache(filesystem[fileSystemIndex].location.occupiedCartridges[0], filesystem[fileSystemIndex].location.occupiedFrames[0], sizeOfFrameBuf);
		// Place the frame into the bus
		runBusRequest(4, 0, filesystem[fileSystemIndex].location.occupiedFrames[0], sizeOfFrameBuf);
		if(regstate.rt != 0) {
			printf("cart_write: error writing to frame %d\n", filesystem[fileSystemIndex].location.occupiedFrames[0]);	
			return -1;
		}	
	} 
	// Code used for writing to the file's exists frames and additionally need frames
	else {
		// Run code within if statement's scope if more frames are needed to accomodate the number of characters to be written (count)
		if((filesystem[fileSystemIndex].filePointer + count) > ((filesystem[fileSystemIndex].location.frames + 1) * CART_FRAME_SIZE)) {
			filesystem[fileSystemIndex].location.frames++; // Increase the number of occupied frames by one, and add the new frame to the aarry.
			myRalloc = realloc(filesystem[fileSystemIndex].location.occupiedFrames, sizeof(int) * (filesystem[fileSystemIndex].location.frames + 1));
			if(myRalloc == NULL) {
				printf("cart_write: filesystem[fileSystemIndex].location.occupiedFrames\n");
				return -1;
			}
			filesystem[fileSystemIndex].location.occupiedFrames = myRalloc;
			filesystem[fileSystemIndex].location.occupiedFrames[filesystem[fileSystemIndex].location.frames] = nextFrame;
			nextFrame++;

			// filesystem[fileSystemIndex].location.cartridges++; // Increase the number of occupied cartridges for this file by one, and add the new cartridge to the array/
			myRalloc = realloc(filesystem[fileSystemIndex].location.occupiedCartridges, sizeof(int) * (filesystem[fileSystemIndex].location.frames + 1));
			if(myRalloc == NULL) {
				printf("cart_write: filesystem[fileSystemIndex].location.occupiedCartridges\n");
				return -1;
			}
			filesystem[fileSystemIndex].location.occupiedCartridges = myRalloc;
			filesystem[fileSystemIndex].location.occupiedCartridges[filesystem[fileSystemIndex].location.frames] = nextCartridge;
			// If the nextFrame is equal to CART_FRAME_SIZE (which does not exist), time to go to the next cartridge.
			if(nextFrame == CART_FRAME_SIZE) {
				nextFrame = 0;
				nextCartridge++;
			}
		}
		
		// In assign2, I was reading each frame the file was occupying.  This was very inefficient and costly, especially since in assign3 the file size can be as large as desired.
		// So to solve this, I am doing some quick math with the file's filePointer and CART_FRAME SIZE to determine which frames are actually needed.
		if((filesystem[fileSystemIndex].filePointer + count) % CART_FRAME_SIZE != 0) {
			startFrameIndex = filesystem[fileSystemIndex].filePointer / CART_FRAME_SIZE;
			endFrameIndex = (filesystem[fileSystemIndex].filePointer + count) / CART_FRAME_SIZE;
		}
		else {
			startFrameIndex = filesystem[fileSystemIndex].filePointer / CART_FRAME_SIZE;
			endFrameIndex = startFrameIndex;
		}
		// Since we know how many frames we need to load, we can make the localBuf significantly smaller, and thus saving memory in the heap
		localBuf = malloc(sizeof(char) * CART_FRAME_SIZE * ((endFrameIndex - startFrameIndex) + 1));
		
		// Read each frame the file is occupying, and place it into the localBuf	
		// We load the frame(s) within this for's scope.  To ensure we do not accidently step outside the filesystem.location.occupiedFrames array,
		// I check the size of filesystem.location.frames (the int that keeps track of the size of filesystem.location.occupiedFrames) too.
		for(i=0; i<=(endFrameIndex - startFrameIndex)/* && i<=filesystem[fileSystemIndex].location.frames*/; i++) {
			// Check if the frame is located in the cache.  If it is not, fetch the frame from the bus.
			get_cart_cache_results = get_cart_cache(filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex]);	
			if(get_cart_cache_results == NULL) {	
				// Load cartridge file is located in if it isn't already loaded 
				if(currentlyLoadedCartridge != filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex]) {
					runBusRequest(2, filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], 0, NULL);
					if(regstate.rt != 0) {
						printf("cart_write: Error loading cartridge %d\n", filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex]);
						return -1;
					}
					currentlyLoadedCartridge = filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex];
				}
				runBusRequest(3, 0, filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex], &localBuf[CART_FRAME_SIZE * i]);
				if(regstate.rt != 0) { // Returns -1 if the frame cannot be read
					printf("cart_write: failed to read cartridge %d frame %d\n", filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex]);
					return -1;
				}
			}
			else {
				// Copy void frame from cache to the local buf
				memcpy(&localBuf[CART_FRAME_SIZE * i], get_cart_cache_results, CART_FRAME_SIZE);
			}
		}

		// Updates the localBuf at offset filesystem[fileSystemIndex].filePointer with count characters from buf
		strncpy(&localBuf[filesystem[fileSystemIndex].filePointer - (startFrameIndex * CART_FRAME_SIZE)], buf, count);
		
		// We write the frame(s) within this for's scope.  To ensure we do not accidently step outside the filesystem.location.occupiedFrames array,
		// I check the size of filesystem.location.frames (the int that keeps track of the size of filesystem.location.occupiedFrames) too.
		for(i=0; i<=(endFrameIndex - startFrameIndex)/* && i<=filesystem[fileSystemIndex].location.frames*/; i++) {
			// Load cartridge file is located in if it isn't already loaded 
			if(currentlyLoadedCartridge != filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex]) {
				runBusRequest(2, filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], 0, NULL);
				if(regstate.rt != 0) {
					printf("cart_write: Error loading cartridge %d\n", filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex]);
					return -1;
				}
				currentlyLoadedCartridge = filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex];
			}

			// Updates the sizeOfFrameBuf from localBuf at offset j * CART_FRAME_SIZE with CART_FRAME_SIZE characters
			strncpy(sizeOfFrameBuf, &localBuf[i * CART_FRAME_SIZE], CART_FRAME_SIZE);
			
			// Place the frame into the cache
			put_cart_cache(filesystem[fileSystemIndex].location.occupiedCartridges[i + startFrameIndex], filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex], sizeOfFrameBuf);
				// Place the frame into the bus
			runBusRequest(4, 0, filesystem[fileSystemIndex].location.occupiedFrames[i + startFrameIndex], sizeOfFrameBuf);
			if(regstate.rt != 0) {
				printf("cart_write: error writing to frame %d\n", i);	
				return -1;
			}	
		}

		// If the length of the file < (filePointer + count), expand the size of the file, and set filePointer equal to length
		if(filesystem[fileSystemIndex].length < (filesystem[fileSystemIndex].filePointer + count)) {
			filesystem[fileSystemIndex].length += count - (filesystem[fileSystemIndex].length - filesystem[fileSystemIndex].filePointer);
			filesystem[fileSystemIndex].filePointer = filesystem[fileSystemIndex].length;
		}
		else {
			filesystem[fileSystemIndex].filePointer += count; // Update file's filePointer += count
		}
		free(localBuf);
	}

	// Return successfully with count bytes written
	return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_read
// Description  : Seek to specific point in the file
//
// Inputs       : fd - filename of the file to write to
//                loc - offfset of file in relation to beginning of file
// Outputs      : 0 if successful, -1 if failure

int32_t cart_seek(int16_t fd, uint32_t loc) {
	int i;
	int fileSystemIndex = -1;	

	for(i=0; i <= fileSystemSize; i++) { // determines fileSystemIndex by looking for the fd in the filesystem array
		if(filesystem[i].fileHandle == fd) {
			fileSystemIndex = i;
			break;
		}
	}
	if(fileSystemIndex == -1) { // returns -1 if the filehandle is bad
		printf("cart_write: filehandle %d is bad\n", fd);
		return -1;
	}
	else if(filesystem[fileSystemIndex].fileHandle == 0) { // returns -1 if the filehandle is not open. A filehandle that equals zero in my filesystem means it is closed
		printf("cart_write: filehandle %d is not open\n", fd);
		return -1;
	}
	
	// Cannot seek if location to seek is greater than the length, so returns -1
	if(filesystem[fileSystemIndex].length < loc) {
		return -1;
	}
 
	filesystem[fileSystemIndex].filePointer = loc;
	
	// Return successfully
	return (0);
}
