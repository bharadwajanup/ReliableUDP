#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdlib.h>
#include<string.h>
#include<limits.h>
#include "udp_header.h"
#define WINDOW 1000


int create_socket, new_socket,port,n,sleep_val=1000;
struct sockaddr_in address;
struct hostent *server;
char *request_pointer,hostname[256],con_type[2],filename[128];
bool close_connection=true;
char *buffer,file_content[1024],p_file_content[1024];
socklen_t fromlen;
double probability;
int q_count=0;


//Node for the queue
struct node
{
  header info;
  char temp_data[256];
  struct node *ptr;
}*front,*rear,*temp,*front1,*curr,*prev;



//Begin: A Data Structure for buffering the packets. More like a queue but the elements are
//ordered based on their sequence number. We could say its a variation of a queue or simply
//an ordered linked list.

void q_create()
{
  front = rear = NULL;
}

void enqueue(header data)
{
  temp = (struct node *)malloc(1*sizeof(struct node));
  temp ->ptr = NULL;
  temp->info = data;
  strcpy(temp->temp_data,data.data);
  if (front == NULL)
  {
    front = temp;
    temp->ptr = NULL;
  }
  else
  {
    curr = front;
    prev = curr;
    if(curr->info.seq_num == temp->info.seq_num)
      return;
    if(curr->info.seq_num > temp->info.seq_num)
    {
      front = temp;
      temp ->ptr = curr;
      
    }
    else
    {
      while(curr != NULL)
      {
	curr = curr->ptr;
	if(curr == NULL)
	{
	  prev->ptr = temp;
	  temp->ptr = curr;
	}
	else
	{
	  if(curr->info.seq_num == temp->info.seq_num)
	    return;
	  if(curr->info.seq_num > temp->info.seq_num)
	  {
	    prev->ptr = temp;
	    temp->ptr = curr;
	  }
	  prev = prev->ptr;
	}
      }
    }
  }
  q_count++;
}


header dequeue()
{
  header value;
  front1 = front;
  
  if (front1 == NULL)
  {
    printf("\n Queue is empty\n");
    return;
  }
  else
  {
    if (front1->ptr != NULL)
    {
      front1 = front1->ptr;
      strcpy(front1->info.data,front1->temp_data);
      value = front1->info;
      free(front);
      front = front1;
    }
    else
    {
      strcpy(front1->info.data,front->temp_data);
      value = front->info;
      free(front);
      front = NULL;
      rear = NULL;
    }
  }
    q_count--;
    return value;
}


header frontelement()
{
  if (front != NULL)
    return(front->info);
}

bool q_empty()
{
  return (front==NULL);
}





//End: 


//Returns true or false based on the probability given.
//Source: Stack Overflow article.
bool introduceLatency(double probability)
{
  return rand() <  probability * ((double)RAND_MAX + 1.0);
}


int sendData(header head)
{
  char charHeader[256];
  
  sprintf(charHeader, "%d:%d:%d:%d:%s\0",head.len,head.ack_indicator,head.seq_num,head.ack_num,head.data);
  
   //printf("\nheader to be sent: %s\n",charHeader);
  
  return sendto(create_socket,charHeader,strlen(charHeader),0,(struct sockaddr *)&address,fromlen);
}


//Sends the file request to the server, acknowledges the packets detects and buffers out of order packets.
void getFileContents(char *filename)
{
  header head,prev_header;
  char buf[1024];
  int expected_sequence=1;
  int bytes_read,content_length,total=0, close_test=0;
  
  
  fromlen = sizeof(address);
  printf("Send request to Fetch the file %s\n",filename);
  
  
  //Build the header
  //head.source_port = port;
  //head.des_port = port;
  head.ack_indicator =1;
  //head.recv_window = WINDOW;
  head.seq_num = 0;
  head.ack_num = 1;
  head.data=filename;
  head.len = strlen(head.data);
  
  
  n=sendData(head); //Sends the initial file name request
  
  if (n < 0)
  {
    error("ERROR writing to socket");
    exit(1);
  }
  
  prev_header = head;
  
  do{
    bzero(buf, 1024);
    n=recvfrom(create_socket,buf,1024,0,(struct sockaddr *)&address,&fromlen);
    if (n < 0)
    {
      error("ERROR reading from socket");
      exit(1);
    }
    
    //Sleep for some time simulating high latency
    if(introduceLatency(probability)) 
      usleep(sleep_val);
    buffer = buf;
    
    //The incoming request will be parsed and a header structure is returned.
    head = parseHeader(buffer); 
      
    //Denotes retransmission of older packets. Simply discard.  
    if(prev_header.ack_num > head.seq_num) 
      continue;
    
    //Handle out of order packets. Put them into the buffer for future use.
    if(prev_header.ack_num < head.seq_num) 
    {
      printf("\n\n\nOut of order packet detected\n\n\n");
      enqueue(head); //Push them into the buffer.
      
      //Handling a boundary scenario where the close signal from the server appears 
      //out of order.
      if(head.ack_indicator <0)
	head.ack_indicator = 0; 
      n = sendData(prev_header); //Duplicate acknowledgement.
      continue;
    }
    if(strcmp(head.data,"close")!=0)
    printf("%s",head.data); //All is well. Print the data received.
    
    if(!q_empty()) //check the buffer to see if we have buffered packets.
    {
      //Our buffer stores them in order. Pop them out and display.
      while((!q_empty()) && (frontelement().seq_num == (head.seq_num + head.len)))
      {
	head = dequeue(); 
	//printf("\nPrinting from the queue\n");
	if(strcmp(head.data,"close")!=0)
	printf("%s",head.data);
  
      }
    }
    if(head.ack_indicator < 0) //End of transfer. Prepare for acknowledgement of the same. -1 denotes end of transfer.
      head.ack_indicator = -1;
    else
      head.ack_indicator=0;
    
    //Sets the ack_num to be sent. Could be cumulative as 
    //we may have drawn packets from the buffer.
    head.ack_num = head.seq_num+head.len; 
    strcpy(head.data,"ack\0");
        
    n = sendData(head); //Sends the acknowledgement
    prev_header = head; // Update the prev_header.
    
    if (n < 0)
      error("ERROR writing to socket");
    
}while(head.ack_indicator>=0);

 //End of file transfer

  

}




int main(int argc, char* argv[])
{
  
  char *p_filename;
  q_create(); //To buffer the packets
  if(argc < 6)
  {
    printf("Invalid number of arguments\nUsage:server_host port file_name latency_probabilty recv_window\n[OPTIONAL]sleep_val\n\n");
    exit(1);
  }
  else if(argc == 6)
  {
    strcpy(hostname,argv[1]);
    strcpy(filename,argv[3]);
    port = atoi(argv[2]);
    probability = strtod(argv[4],NULL);
  }
  else
  {
    
    strcpy(hostname,argv[1]);
    strcpy(filename,argv[3]);
    port = atoi(argv[2]);
    probability = strtod(argv[4],NULL);
    sleep_val = atoi(argv[6]);
  }
  create_socket = socket(AF_INET, SOCK_DGRAM, 0); //Creates a datagram socket
  if(create_socket>0)
    printf("Socket has been created\n");
  
  server = gethostbyname(hostname);
  if(server == NULL)
  {
    printf("Invalid server name: %s\n",hostname);
    exit(1);
  }
  //do a bzero here
  
  address.sin_family = AF_INET;
  bcopy((char *)server->h_addr,
	(char *)&address.sin_addr.s_addr,
	server->h_length);
  address.sin_port = htons(port);
  
  getFileContents(filename); 
  
  close(create_socket);
  return 0;
  
}
