#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Constants
#define WINDOWSIZE 10
#define MAXWAITTIME_SECONDS 1
#define SENDSIZE 2
#define HEADERSIZE 15
#define BUFFER_SIZE 100
#define USAGE_MSG "USAGE: client <ipaddr> <portnumber>\n"

// Functions
void buildClient(char serverIP[], int port);
void getStringToSend(char *buffer);
int sendString(char *buffer, int sd, struct sockaddr_in servaddr);
void sendStringSize(int sizeOfString, int sd, struct sockaddr_in servaddr);
int sendStringWindow(int seq, int offset, int totalBytesToSend, int sd,
                     struct sockaddr_in servaddr, char bufferRead[]);
int sendStringSegments(int seq, char *segment, int sd, struct sockaddr_in servaddr);

int main(int argc, char *argv[])
{
  // check there is enough arguments
  if (argc < 3)
  {
    printf(USAGE_MSG);
    exit(1);
  }

  // loosely check if address is legitimate
  int addrlen = strlen(argv[1]);
  if (addrlen > 15 || addrlen < 7)
  {
    printf("Address does not exist.\n");
    printf(USAGE_MSG);
    exit(1);
  }

  // loosely check if address is legitimate
  int port = atoi(argv[2]);
  if (port > 65535 || port < 0)
  {
    printf("Port does not exist.\n");
    printf(USAGE_MSG);
    exit(1);
  }

  // Create client
  buildClient(argv[1], port);

  return 0;
}

void buildClient(char serverIP[], int portNumber)
{
  struct sockaddr_in servaddr;
  int sd, rc;
  char buffer[BUFFER_SIZE];

  // create socket
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0)
  {
    printf("socket creation failed...\n");
    exit(1);
  }
  printf("Socket successfully created...\n");

  // set server info
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(portNumber);
  servaddr.sin_addr.s_addr = inet_addr(serverIP);

  // get string form user
  getStringToSend(buffer);

  // send full string in window-size chunks, 2 bytes at a time
  rc = sendString(buffer, sd, servaddr);

  // let user know if send succeeded
  if (rc < 0)
    printf("Message failed to send\n");
  else
    printf("Message sent!\n");

  // clean up client
  close(sd);
}

void getStringToSend(char *buffer)
{
  bzero(buffer, sizeof(buffer));
  printf("Enter message to send:\n");
  scanf("%[^\n]s", buffer);

  // handles messages empty or too large
  if (strlen(buffer) > BUFFER_SIZE)
  {
    printf("Message size is too many characters. ");
    printf("(%ld > %d)\n", strlen(buffer), BUFFER_SIZE);
    exit(1);
  }
  else if (strlen(buffer) == 0)
  {
    printf("Nothing to send.\n");
    exit(1);
  }

  printf("You are sending '%s'\n", buffer);
}

int sendString(char *buffer, int sd, struct sockaddr_in servaddr)
{
  int ackNumber,
      totalBytesToSend = strlen(buffer),
      bytesLeftToSend = totalBytesToSend,
      endOfWindow = 0,
      startOfWindow = 0;

  // initiate meesage sending with size of message
  sendStringSize(totalBytesToSend, sd, servaddr);

  // handles if message is larger or smaller than window size
  if (bytesLeftToSend > WINDOWSIZE)
    endOfWindow = WINDOWSIZE;
  else
    endOfWindow = bytesLeftToSend;

  // sends message in chunk no more that window size at a time
  while (endOfWindow <= totalBytesToSend && bytesLeftToSend > 0)
  {
    // send window
    ackNumber = sendStringWindow(startOfWindow, endOfWindow,
                                 totalBytesToSend, sd,
                                 servaddr, buffer);

    // if there was a bad reponse or no reponse
    if (ackNumber <= 0 || ackNumber == startOfWindow - 2)
    {
      printf("Sent %d out of %d bytes\n", startOfWindow, totalBytesToSend);
      return -1;
    }

    // set up next window based on server response
    startOfWindow = ackNumber + SENDSIZE;
    bytesLeftToSend = totalBytesToSend - startOfWindow;
    endOfWindow = startOfWindow;

    // handles if more bytes to send than window size
    if (bytesLeftToSend > 10)
      endOfWindow += 10;
    else
      endOfWindow += bytesLeftToSend;
  }

  // let user know all bytes sent a return no error
  printf("Sent all %d bytes\n", totalBytesToSend);
  return 0;
}

void sendStringSize(int sizeOfString, int sd, struct sockaddr_in servaddr)
{
  // convert int to network short to send
  sizeOfString = htonl(sizeOfString);
  sendto(sd, &sizeOfString, sizeof(sizeOfString), 0,
         (struct sockaddr *)&servaddr, sizeof(servaddr));
}

int sendStringWindow(int seqNumber, int offset, int totalBytesToSend,
                     int sd, struct sockaddr_in servaddr, char *buffer)
{
  struct sockaddr_in fromAddress;
  socklen_t fromLength = sizeof(struct sockaddr_in);
  time_t timeSent;
  char bufferRead[BUFFER_SIZE],
      segment[SENDSIZE + 1];
  int rc,
      ackNumber,
      startingSeq = seqNumber;

  for (int j = seqNumber; j < offset; j += SENDSIZE)
  {
    // handles if there is 1 or 2 bytes at the end
    if (j + 1 < totalBytesToSend)
      sprintf(segment, "%c%c", buffer[j], buffer[j + 1]);
    else
      sprintf(segment, "%c", buffer[j]);

    // send segments of window
    rc = sendStringSegments(seqNumber, segment, sd, servaddr);
    seqNumber += SENDSIZE;
  }

  for (int i = startingSeq; i < offset; i += 2)
  {
    bzero(bufferRead, sizeof(bufferRead));
    // get reponse from sever to verify last segment successfully recieved
    timeSent = time(NULL);
    while(time(NULL) - timeSent < MAXWAITTIME_SECONDS)
    {
      rc = recvfrom(sd, &bufferRead, sizeof(bufferRead), MSG_DONTWAIT,
                      (struct sockaddr *)&fromAddress, &fromLength);
      if(rc >= 0)
        break;
    }
    sscanf(bufferRead, "%11d", &ackNumber);

    // detects error with server to stop client
    if (ackNumber < 0 || ackNumber > BUFFER_SIZE)
      return -1;
  }

  return ackNumber;
}

int sendStringSegments(int seq, char *segment, int sd, struct sockaddr_in servaddr)
{
  char message[HEADERSIZE + SENDSIZE];
  int rc;

  // create protocol message with headers and segment and send
  sprintf(message, "%11d%4ld%s", seq, strlen(segment), segment);
  // printf("Sending | %s\n",message); // DEBUG helper
  rc = sendto(sd, &message, strlen(message), 0,
              (struct sockaddr *)&servaddr, sizeof(servaddr));

  return rc;
}
