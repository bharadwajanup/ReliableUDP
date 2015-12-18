struct rel_udp_header
{
  //int source_port;
  //int des_port;
  int len;
  int ack_indicator; //0=acknowledgement 1=data -1:close
 // int recv_window;
  int seq_num;
  int ack_num;
  char *data;
};


typedef struct rel_udp_header header;


header parseHeader(char *req)
{
  header head;
  //head.source_port = atoi(strtok(req,":"));
  //head.des_port = atoi(strtok(NULL,":"));
  head.len = atoi(strtok(req,":"));
  head.ack_indicator = atoi(strtok(NULL,":"));
  //head.recv_window = atoi(strtok(NULL,":"));
  head.seq_num = atoi(strtok(NULL,":"));
  head.ack_num = atoi(strtok(NULL,":"));
  head.data = strtok(NULL,"\0");
 // printf("--------------------------------------------\n");
  //printf("source:%d\tdestination:%d\t\nlength:%d\nack:%d\treceive_window:%d\nseq_num:%d\nack_num:%d\ndata:%s\n",head.source_port,head.des_port,head.len,head.ack_indicator,head.recv_window,head.seq_num,head.ack_num,head.data);
  //printf("--------------------------------------------\n");
  
  return head;
}
