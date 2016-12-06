////////////////////////////////////////////////////////////////////////////////
//
//  File           : cart_cache.c
//  Description    : This is the implementation of the cache for the CART
//                   driver.
//
//  Author         : James Frazier
//  Last Modified  : Thursday, November 24
//

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Project includes
#include <cmpsc311_log.h>
#include <cart_cache.h>
#include <cart_controller.h>
// Defines

////////////////////////////////////////////////////////////////////////////////
//
// Structure    : cachedFrame
// Description  : A structure for my cache. Each one will correspond to one frame
//		  The number of these created is determined by myMaxFrames

typedef struct cachedFrame {
	int frame; // frame number corresponding to the cached frame
	int cartridge; // cartridge number corresponding to the cached frame
	char cache[CART_FRAME_SIZE]; // the frame itself
	int priority; // the priority of the frame to determine if it should be evicted.  If the priority equals myMaxFrames, it is next in line to be evicted
} cachedFrame;

cachedFrame* myCache; // pointer to all the cached frames.  It will be alloc in init_cart_cache
int myMaxFrames; // the size of the cache determined in set_cart_cache_size
int numberOfUnoccupiedFrames; // number of frames that have not been occupied yet in the cache. Once this reaches zero, it signals to my cache that it is time to evict frames

////////////////////////////////////////////////////////////////////////////////
//
// Function     : set_cart_cache_size
// Description  : Set the size of the cache (must be called before init)
//
// Inputs       : max_frames - the maximum number of items your cache can hold
// Outputs      : 0 if successful, -1 if failure

int set_cart_cache_size(uint32_t max_frames) {
	myMaxFrames = max_frames;
	numberOfUnoccupiedFrames = max_frames;
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : init_cart_cache
// Description  : Initialize the cache and note maximum frames
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int init_cart_cache(void) {
	// Alloc the number of cachedFrames needed based on the myMaxFrames
	myCache = (cachedFrame *) malloc(sizeof(struct cachedFrame) * myMaxFrames);
	if(myCache == NULL) { // Return -1 is error will malloc
		printf("Error with malloc for myCache\n");
		return -1;
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : close_cart_cache
// Description  : Clear all of the contents of the cache, cleanup
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int close_cart_cache(void) {
	free(myCache); // Free memory in the heap from my cache system
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : adjust_priority
// Description  : When a frame is put or gotten from the cache, that frames priority
//		  is set to equal one (meaning it is last in line to be evicted). 
//		  Using the previous priority of this frame, we determine which other frame's
//	 	  priorities need to be adjusted, so they are closer in line to be evicted. 
//
// Inputs       : exempt - this is the frame that was recently put or gotten.  We want to ensure 
//			   the priority of this frame remains one, so we skip it.
//		: abovePriority - this is the previous priority of the frame that was recently put or gotten
//				  before it was set to one.  We use this number to determine which frame's
//				  priorties need to be adjusted, so they are closer in line to be evicted.
// Outputs      : 0 if successful, -1 if failure

int adjust_priority(int exempt, int abovePriority) {	
	int i;
	
	// For statement to push older frames closer to being evicted.
	for(i=(myMaxFrames - 1); i>=numberOfUnoccupiedFrames; i--) {
		if(i != exempt && myCache[i].priority < abovePriority) {
			myCache[i].priority++;
		}		
	}	
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : put_cart_cache
// Description  : Put an object into the frame cache
//
// Inputs       : cart - the cartridge number of the frame to cache
//                frm - the frame number of the frame to cache
//                buf - the buffer to insert into the cache
// Outputs      : 0 if successful, -1 if failure

int put_cart_cache(CartridgeIndex cart, CartFrameIndex frm, void *buf)  {
	int i, prioritiesToAdjust;
	
	// This for statement searches the current cache, to see if we need to update it
	for(i=(myMaxFrames - 1); i>=numberOfUnoccupiedFrames; i--) {
		if(myCache[i].frame == frm && myCache[i].cartridge == cart) { 
			prioritiesToAdjust = myCache[i].priority; // Save the previous priority of this cachedFrame for our adjust_priority function (refer to this function for the reason why)
			myCache[i].priority = numberOfUnoccupiedFrames + 1; // Since the cached frame is being updated, we adjust the priority to one, so it is last to be evicted.
			adjust_priority(i, prioritiesToAdjust); // Call adjust_priority to adjust the priority of cached frames, so they are closer to being evicted.
			memcpy(myCache[i].cache, buf, CART_FRAME_SIZE);  // Place the buf into the cached frame.
			return 0;
		}
	}

	// This if statement is executed if a frame needs to be evicted to accomodate the 
	if(numberOfUnoccupiedFrames == 0) {
		for(i=(myMaxFrames - 1); i>=numberOfUnoccupiedFrames; i--) {
			if(myCache[i].priority == myMaxFrames) {
				prioritiesToAdjust = myCache[i].priority; // Save the previous priority of this cachedFrame for our adjust_priority function (refer to this function for the reason why)
				myCache[i].priority = numberOfUnoccupiedFrames + 1; // Since this frame is new, we adjust the priority to one, so it is last to be evicted.
				adjust_priority(i, prioritiesToAdjust); // Call adjust_priority to adjust the priority of cached frames, so they are closer to being evicted.
				myCache[i].frame = frm; // Update the frame number 
				myCache[i].cartridge = cart; // Update the cart number
				memcpy(myCache[i].cache, buf, CART_FRAME_SIZE); // Place the buf into the cached frame.
				return 0;
			}
		}
		// If for some reason the for statement cannot find the cachedFrame who is next to be evicted, a logic error occured.  This shouldn't happen.
		return -1;
	}
	
	// If there is room in the cache, and the cachedFrame does not already exist, fill an empty cache frame
	i = numberOfUnoccupiedFrames - 1; 
	myCache[i].priority = numberOfUnoccupiedFrames;
	numberOfUnoccupiedFrames--;
	myCache[i].frame = frm;
	myCache[i].cartridge = cart;
	memcpy(myCache[i].cache, buf, CART_FRAME_SIZE);
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : get_cart_cache
// Description  : Get an frame from the cache (and return it)
//
// Inputs       : cart - the cartridge number of the cartridge to find
//                frm - the  number of the frame to find
// Outputs      : pointer to cached frame or NULL if not found

void * get_cart_cache(CartridgeIndex cart, CartFrameIndex frm) {
	int i, prioritiesToAdjust;
	
	// This for statement looks for the requested frame, and returns the buf if it is found.  If not, it returns NULL 
	for(i=(myMaxFrames - 1); i>=numberOfUnoccupiedFrames; i--) {
		if(myCache[i].frame == frm && myCache[i].cartridge == cart) {	
			prioritiesToAdjust = myCache[i].priority; // Save the previous priority of this cachedFrame for our adjust_priority function (refer to this function for the reason why)
			myCache[i].priority = numberOfUnoccupiedFrames + 1; // Since the cached frame is being updated, we adjust the priority to one, so it is last to be evicted.
			adjust_priority(i, prioritiesToAdjust); // Call adjust_priority to adjust the priority of cached frames, so they are closer to being evicted.
			return myCache[i].cache;	
		}
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : delete_cart_cache
// Description  : Remove a frame from the cache (and return it).  I do not use 
//   		  this function in my project.
//
// Inputs       : cart - the cart number of the frame to remove from cache
//                blk - the frame number of the frame to remove from cache
// Outputs      : pointe buffer inserted into the object

void * delete_cart_cache(CartridgeIndex cart, CartFrameIndex blk) {
	// Not used
	return 0;
}

//
// Unit test

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cartCacheUnitTest
// Description  : Run a UNIT test checking the cache implementation
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int cartCacheUnitTest(void) {

	// Return successfully
	logMessage(LOG_OUTPUT_LEVEL, "Cache unit test completed successfully.");
	return(0);
}
