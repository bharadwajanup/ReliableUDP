#include<netinet/in.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<unistd.h>
#include<string.h>
#include<fcntl.h>
#include<stdbool.h>
#include<sys/time.h>
#include<math.h>
#include<limits.h>
#include "udp_header.h"
#define BUF_SIZE 1024
#define DATA_PAYLOAD_SIZE 100



long  timeout=0,dev_rtt=0,est_rtt=0,sample_rtt=0;
int create_socket, new_socket;
socklen_t fromlen;
char buffer[BUF_SIZE], port[6];
struct sockaddr_in address;
int n,window=0,cwnd=0,rwnd=0,ssthresh=64000,slowStartCounter=0,congestionAvoidanceCounter=0,totalPackets;
double probability;
int MSS = (int) sizeof(header) + DATA_PAYLOAD_SIZE;

//Variables for analysis
int max_congestion_window=0,max_packets_sent=0,packet_counter=0;;




void setupSocket()//Function to create and bind the socket to a port
{
  if ((create_socket = socket(AF_INET, SOCK_DGRAM, 0)) > 0){
    printf("The socket was created\n");
  }
  
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(atoi(port));
  
  if (bind(create_socket, (struct sockaddr *) &address, sizeof(address)) == 0){
    printf("Binding Socket at port %d\n" ,atoi(port));
  }
  else
  {
    perror("server:bind");
    exit(1);
  }
  
  
}



//Probabilistically drop packets.
//Source: Stack overflow article.
bool dropPacket(double probability)
{
  return rand() <  probability * ((double)RAND_MAX + 1.0);
}

/*bool dropPacket(double probability)
 * {
 *  return false;
 }*/


//Calculates the timeout using Jacobson/Kragel's algorithm.
struct timeval calculateTimeout(long sample_rtt)
{
  struct timeval struct_timeout;
  est_rtt = (0.875 * est_rtt) + (0.125 * sample_rtt);
  
  dev_rtt = (0.75 * dev_rtt) + (0.25 * labs(sample_rtt - est_rtt));
  
  timeout = est_rtt + 4 * dev_rtt;
  
  printf("RTT: %ld\n",timeout);
  if(timeout == 0)
    timeout++;
  struct_timeout.tv_sec = timeout/1000000;
  struct_timeout.tv_usec = timeout % 1000000;
  
  return struct_timeout;
  
}



int find_minimum(int a, int b)
{
  if(a>b)
    return b;
  else
    return a;
}

//Sends the data to the client.
int sendData(header head)
{
  char charHeader[256];
  sprintf(charHeader, "%d:%d:%d:%d:%s",head.len,head.ack_indicator,head.seq_num,head.ack_num,head.data);
  
  printf("\nheader to be sent: %s\n",charHeader);
  
  return sendto(create_socket,charHeader,strlen(charHeader),0,(struct sockaddr *)&address,fromlen);
}


//Function to implement congestion control. Indicates a timeout of parameter isTimeout is true.
void updateCongestionWindow(bool isTimeOut)
{
  if(isTimeOut)
  {
    printf("\n Now in slow start mode as packet timed out... \n");
    ssthresh = cwnd/2;
    cwnd = MSS;
    slowStartCounter++;
  }
  else
  {
    if(cwnd<ssthresh)
    {
      cwnd = cwnd + MSS;
      slowStartCounter++;
    }
    else
    {
      printf("\n In congestion avoidance mode...\n");
      cwnd = cwnd + MSS * (MSS/cwnd);
      congestionAvoidanceCounter++;
    }
  }
  max_congestion_window = cwnd>max_congestion_window?cwnd:max_congestion_window;
}



void fetchFileContents(header head)
{
  char fileBuf[2048],packetData[2048];
  int mem=500,packet_size = 0;
  char *fileContent = (char*) malloc(mem*sizeof(char));
  int counter =0,index =0;
  int last_ack=1,expected_ack,last_sent;
  FILE *fp;
  struct timeval tv,start,end;
  struct stat st;
  int filesize,dup_ack_count=0,read_char = DATA_PAYLOAD_SIZE;
  bool first_packet_switch=true;
  
  //Initial timeout value of 1 second.
  tv.tv_sec = 1;
  tv.tv_usec =0;
  max_packets_sent=0;
  max_congestion_window=0;
  printf("Fetch the contents of the file: %s\n" ,head.data);
  fp = fopen(head.data,"r");
  
  if(fp == NULL)
  {
    //Send close signal to the client as the file is not found.
    printf("File not found\n");
    head.ack_indicator = -1;
    head.ack_num++;
    strcpy(head.data, "close"); //indicates the client to close its connection.
    
    
    n = sendData(head);
    if(n<0)
    {
      printf("Unable to write\n");
    exit(1);
    }
    return;
  }
  stat(head.data,&st);
  filesize = st.st_size;
  printf("File size: %d\n",(int)st.st_size);
  if(filesize > (INT_MAX - 1)) //Limitation on the file size.
  {
    printf("File Size too big\n");
    return;
  }
  strcpy(fileContent,"");
  //Reads the contents of the file.
  while(!feof(fp))
  {
    n = fread(fileBuf,sizeof(char),1,fp);
    if(n>0)
      strcat(fileContent,fileBuf);
    counter+=n;
    if(counter>= (mem-1))
    {
      mem+=500;
      fileContent = realloc(fileContent, mem);
    }
  }
  strcat(fileContent,"\0");
  
  cwnd = MSS; 
  window = find_minimum(cwnd,rwnd);
  
  //Start sending packet by packet
  // while(index<strlen(fileContent))
  if(strlen(fileContent) < DATA_PAYLOAD_SIZE)
  {
    expected_ack = strlen(fileContent) + 1;
  }
  else
  {
    expected_ack = DATA_PAYLOAD_SIZE + 1;
  }
  while(head.ack_indicator >=0)
  {
    window = find_minimum(cwnd, rwnd);
    //Sets the time period for which the server should listen for acknowledgements.
    setsockopt(create_socket,SOL_SOCKET,SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    packet_counter=0;
    first_packet_switch=true; //Safe set to true.
    while((window >= MSS) && (index < strlen(fileContent)))
    {
      
      strcpy(packetData,"");
      strncat(packetData,&fileContent[index],DATA_PAYLOAD_SIZE);
      head.ack_indicator=1;
      head.seq_num = index+1;
      head.data = packetData;
      head.len = strlen(packetData);
      //Chooses to send or drop packets based on the return boolean.
      if(!dropPacket(probability))
	n=sendData(head); //Send data.
	if(first_packet_switch)
	{
	  gettimeofday(&start,NULL);
	  first_packet_switch = false;
	}
      window = window - MSS;
      index = head.seq_num + head.len -1;
      last_sent = head.seq_num;
      packet_counter++;
    }
    max_packets_sent = packet_counter>max_packets_sent?packet_counter:max_packets_sent;
    
    
    //Check if all the contents of the file have been sent. Prepare for sending the close signal
    //to the client
    if((expected_ack > index) && (index>=strlen(fileContent)))
    {
      head.ack_indicator=-1;
      strcpy(head.data, "close"); //indicates the client to close its con.
      head.seq_num = index+1;
      head.len = strlen(head.data);
      n=sendData(head);
      last_sent = head.seq_num;
    }
    
    //Listen to acknowledgements until timeout.
    while(last_sent >= last_ack)
    {
      
      
      strcpy(packetData,"");
      
      n=recvfrom(create_socket,packetData,1024,0,(struct sockaddr *)&address,&fromlen);
      
      
      if (n < 0){
	//Packet timed out. Resend the packet.
	printf("Socket timedout: resending the packet\n");
	window = window + (int) sizeof(header) + head.len;
	//Congestion window should now be 1MSS
	updateCongestionWindow(true); 
	if(head.ack_indicator <0) //Ensure that the loop won't end after resend.
	  head.ack_indicator = 1;
	break;
      }
      
      
      
      
      
      //printf("The Client sent:\n%s",packetData);
      printf("Packet received\n");
      head = parseHeader(packetData);
      last_ack = head.ack_num;
      window = window + (int) sizeof(header) + head.len;
      if(head.ack_num < expected_ack )
      {
	//index = head.ack_num-1;
	dup_ack_count++;
	if(dup_ack_count == 3)
	{
	  //Resend the packet after 3 duplicate acks from the client.
	  dup_ack_count =0;
	  ssthresh = cwnd/2;
	  cwnd = ssthresh + 3 * MSS;
	  index = head.ack_num - 1;
	  strcpy(packetData,"");
	  strncat(packetData,&fileContent[index],DATA_PAYLOAD_SIZE);
	  head.ack_indicator=1;
	  head.seq_num = index+1;
	  head.data = packetData;
	  head.len = strlen(packetData);
	  n=sendData(head); //Send data.

	  continue;
	}
	else
	  continue;
      }
      else
      {
	if(!first_packet_switch)
	{
	  gettimeofday(&end,NULL);
	  first_packet_switch = true;
	  sample_rtt = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);
	  printf("Sample_rtt = %ld\n",sample_rtt);
      
      //Get the new RTT value. now the server listens to the acks from the client with the 
      //updated value.
      tv = calculateTimeout(sample_rtt);
      
	}
	//acks are in order.
	expected_ack = head.ack_num + head.len;
	dup_ack_count=0;
      }
      printf("Ack received with ack_num:%d\n",head.ack_num);
      //Recalculate the congestion window
      updateCongestionWindow(false);
      
    }
    index = head.ack_num - 1;
    
    
  }
  totalPackets = slowStartCounter + congestionAvoidanceCounter;
  printf("Number of packets sent: %d\n",totalPackets);
  printf("Number of packets sent at slow start: %d\n",slowStartCounter);
  printf("Number of packets sent at Congestion Avoidance: %d\n",congestionAvoidanceCounter);
  printf("Percentage of packets in slow start: %.2f\n",(double)((double)slowStartCounter/totalPackets)*100);
  printf("Percentage of packets in congestion avoidance: %.2f\n",(double)((double)congestionAvoidanceCounter/totalPackets)*100);
  printf("Congestion window grew till %d\n",max_congestion_window);
  printf("At once, the max number of packets that were sent was: %d\n",max_packets_sent);
  
  
  free(fileContent);
  
}

//Experienced some problems with the pointers without this function.
//Therefore, chose to keep this function as is.
void process_request(header head)
{ 
  
  fetchFileContents(head);
  
}


//Reset the values to their defaults.
void resetWindowsandCounters()
{
  window=0;
  cwnd=0;
  ssthresh=64000;
  slowStartCounter=0;
  congestionAvoidanceCounter=0;
  timeout=0;
  dev_rtt=0;
  est_rtt=0;
  sample_rtt=0;
}


int main(int argc, char* argv[]) {
  
  header head;
  struct timeval tv;
  int n;
  
  tv.tv_sec = 0;
  tv.tv_usec =0;
  
  if(argc != 4)
  {
    printf("Invalid number of arguments\nUsage:port drop_probability receiver_window\n");
    exit(1);
  }
  else
  {
    strcpy(port, argv[1]);
    probability = strtod(argv[2],NULL);
    rwnd = atoi(argv[3]);
  }
  
  setupSocket();
  fromlen = sizeof(address);
  while (1) {
    printf("Waiting to receive\n");
    resetWindowsandCounters();
    bzero(buffer,1024);
    n = recvfrom(create_socket,buffer,1024,0,(struct sockaddr *)&address,&fromlen);
    printf("Initial request: %s\n",buffer);
    if (n < 0)
    {
      perror("recvfrom");
      exit(1);
    }
    head = parseHeader(buffer);
    process_request(head);
    
    //Put this at the end, always. This is to reset the recvfrom configuration.
    n = setsockopt(create_socket,SOL_SOCKET,SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    
    if(n<0)
    {
      printf("Error in setting socket options\n");
      exit(1);
    }
  }
  close(create_socket);
  return 0;
}
