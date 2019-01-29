#include "ArtNet.h"
#include <semaphore.h>

artPoll_t artPoll;
artDMX_t artDMX;
int sock;

struct sockaddr_in	 addr, recAddr; 

//artNetDestination destinations[NUMDEST];
int destinations[NUMDEST];

// Semaphores
sem_t semDest; // Lock the destinations array
	
int main() { 
	
	printf("\n** Press ctrl-C to close **\n");
	
	setupArtNet();
	
	signal(SIGINT, intHandler);
	
	sem_init(&semDest, 0, 1);
	
	pthread_t artPollTid;
	pthread_t artPollReplyTid;
	
	memset(destinations, 0, sizeof(destinations));
	
	memcpy(artDMX.Data, dmxValues[0], sizeof(artDMX.Data));
	
	// Creating socket file descriptor 
	if ( (sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	} 

	int broadcastEnable = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, 
		&broadcastEnable, sizeof(broadcastEnable));

	// Create Polling Thread
	pthread_create(&artPollTid, NULL, artPollThread, NULL);
	
	// Create Reply thread
	pthread_create(&artPollReplyTid, NULL, artPollReplyThread, NULL);
	
	struct sockaddr_in addr; 
	
	memset(&addr, 0, sizeof(addr));
	
	// Filling server information 
	addr.sin_family = AF_INET; 
	addr.sin_port = htons(PORT); 
	addr.sin_addr.s_addr = INADDR_ANY; 
	 
	while(1)
		{
			uint8_t i;
			
			sem_wait(&semDest);
			for(i = 0; i < NUMDEST; i++)
			{
				if (destinations[i] != 0)
				{
					addr.sin_addr.s_addr = destinations[i];

					// Repeatedly send dmx packet
					sendto(sock, (uint8_t *)&artDMX, sizeof(artDMX_t),
						MSG_CONFIRM, (const struct sockaddr *) &addr, 
						sizeof(addr)); 		
				}
			}
			sem_post(&semDest);
			artDMX.Sequence++;
			usleep(100000);
		}
	 	
	return 0; 
} 

void intHandler(int blah)
{
	printf("\n** Closing socket and ending. **\n");
	close(sock);
	exit(0);
}

void setupArtNet()
{
	memcpy(artPoll.ID, "Art-Net\0", 8);
	artPoll.OpCode = 0x2000;
	artPoll.ProtVerHi = 0;
	artPoll.ProtVerLo = 14;
	artPoll.TalkToMe = 0x00;
	artPoll.Priority = 0;
	
	memcpy(artDMX.ID, "Art-Net\0", 8);
	artDMX.OpCode = 0x5000;
	artDMX.ProtVerHi = 0;
	artDMX.ProtVerLo = 14;
	artDMX.Physical = 0;
	artDMX.SubUni = 0;
	artDMX.Sequence = 0;
	artDMX.Net = 0;
	artDMX.LengthHi = 0x02;
	artDMX.LengthLo = 0;
}

void setData(uint8_t hiLo, uint8_t addr, uint8_t value)
{
	dmxValues[hiLo][addr - 1] = value;
}



void *artPollThread(void* vArg)
{
	struct sockaddr_in	 addr;
		
	bzero(&addr, sizeof(addr));
	
	// Filling server information 
	addr.sin_family = AF_INET; 
	addr.sin_port = htons(PORT); 
	addr.sin_addr.s_addr = INADDR_ANY; 
	
	if ( bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
		perror("bind");
	
	addr.sin_addr.s_addr = inet_addr(POLLADDR);
		
	while(1)
	{
		sendto(sock, (uint8_t *)&artPoll, sizeof(artPoll_t), 
			MSG_CONFIRM, (const struct sockaddr *) &addr, 
			sizeof(addr)); 
		
		sleep(3);
	}
	
	return NULL;
}

	/*
	 * Create a thread that listens for ArtPollReply messages
	 * When one is received, find the first open spot in the addrs array and add it there
	 * Then repeat
	 */
void *artPollReplyThread(void* vArg)
{
	uint8_t recvBuff[MAXLINE];
	struct sockaddr_in recAddr; 
	uint32_t recAddrSize = sizeof(recAddr);
	
	bzero(&recAddr, sizeof(addr));
	addr.sin_family = AF_INET; 
	addr.sin_port = htons(PORT); 
	addr.sin_addr.s_addr = INADDR_ANY;
	
	//int destArraySize = sizeof(destinations)/sizeof(artNetDestination);
	
	while(1)
	{		
		recvfrom(sock, recvBuff, sizeof(recvBuff), 0, (struct sockaddr*) &recAddr, &recAddrSize);
		artPollReply_t *artPollReply = (artPollReply_t*)recvBuff;
			
		if (artPollReply->OpCode == 0x2100)
		{
			char addressString[20] = "";
			sprintf(addressString, "%d.%d.%d.%d",
				artPollReply->IpAddr[0],
				artPollReply->IpAddr[1],
				artPollReply->IpAddr[2],
				artPollReply->IpAddr[3]);
				
				
			
			int newAddr = inet_addr(addressString);
			
			uint8_t spot = addrExists(destinations, NUMDEST, newAddr); 
			
			if (spot)
			{
				printf("Adding IP: %s\n", addressString);
				addDestination(spot, newAddr);
			}
			
			//uint8_t spot = addrExists(destinations, destArraySize, newAddr);
					
			/*if(spot)
			{
				uint8_t *spotPtr = (uint8_t*)malloc(sizeof(uint8_t));
				
				printf("Adding IP: %s\n", addressString);
				
				artNetDestination newDest;
				
				newDest.addr = newAddr;
				newDest.count = COUNTMAX;
				spotPtr = &spot;
				pthread_create(&newDest.tid, NULL, artDMXThread, spotPtr);
				
				destinations[spot] = newDest;
			}*/
		}
	}
		
	return NULL;
}

/*
 * Looks for a given int value in an array
 * 
 * Returns zero if found, or first available spot otherwise.
 */
uint32_t addrExists(int destArray[], uint8_t size, int val)
{
	uint8_t i, firstAvail = 0;
	
	for (i = 0; i < size; i++)
	{
		if (destArray[i] == val)
			return 0;
		else if (destArray[i] == 0 && firstAvail == 0)
			firstAvail = i;
	}
	
	return firstAvail; // have to wait until all checked to avoid duplicates, 1,2,3,4 --> 1,0,3,4 --> 1,4,3,4
}

void addDestination(uint8_t spot, int newAddr)
{
	sem_wait(&semDest);
	destinations[spot] = newAddr;
	sem_post(&semDest);
}

/*void *artDMXThread(void* vArg)
{
	int destLoc = *((int*) vArg);
	pthread_detach(pthread_self());
	//free(vArg);
	uint16_t count = getCount(destLoc);
	struct sockaddr_in addr; 
	
	memset(&addr, 0, sizeof(addr));
	
	// Filling server information 
	addr.sin_family = AF_INET; 
	addr.sin_port = htons(PORT); 
	addr.sin_addr.s_addr = INADDR_ANY; 
	
	addr.sin_addr.s_addr = destinations[destLoc].addr;

	while(count != 0)
	{
		// Repeatedly send dmx packet
		sendto(sock, (uint8_t *)&artDMX, sizeof(artDMX_t),
			MSG_CONFIRM, (const struct sockaddr *) &addr, 
		sizeof(addr)); 
		artDMX.Sequence++;
		count = decCount(destLoc);
	}
	
	return NULL;
}*/


/*
 * These three functions are thread unsafe
 */ 
/*uint16_t getCount(uint8_t destLoc)
{
	return destinations[destLoc].count;
}

uint16_t decCount(uint8_t destLoc)
{
	destinations[destLoc].count--;
	return destinations[destLoc].count;
}

void setCount(uint8_t destLoc, uint16_t val)
{
	destinations[destLoc].count = val;
}*/
