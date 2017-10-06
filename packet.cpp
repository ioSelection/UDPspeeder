/*
 * packet.cpp
 *
 *  Created on: Sep 15, 2017
 *      Author: root
 */


#include "common.h"
#include "log.h"
#include "packet.h"

int iv_min=2;
int iv_max=16;//< 256;
u64_t packet_send_count=0;
u64_t dup_packet_send_count=0;
u64_t packet_recv_count=0;
u64_t dup_packet_recv_count=0;
typedef u64_t anti_replay_seq_t;
int disable_replay_filter=0;

int random_drop=0;

char key_string[1000]= "secret key";

int local_listen_fd=-1;


void encrypt_0(char * input,int &len,char *key)
{
	int i,j;
	if(key[0]==0) return;
	for(i=0,j=0;i<len;i++,j++)
	{
		if(key[j]==0)j=0;
		input[i]^=key[j];
	}
}
void decrypt_0(char * input,int &len,char *key)
{

	int i,j;
	if(key[0]==0) return;
	for(i=0,j=0;i<len;i++,j++)
	{
		if(key[j]==0)j=0;
		input[i]^=key[j];
	}
}
int do_obscure(const char * input, int in_len,char *output,int &out_len)
{
	//memcpy(output,input,in_len);
//	out_len=in_len;
	//return 0;

	int i, j, k;
	if (in_len > 65535||in_len<0)
		return -1;
	int iv_len=iv_min+rand()%(iv_max-iv_min);
	get_true_random_chars(output,iv_len);
	memcpy(output+iv_len,input,in_len);

	output[iv_len+in_len]=(uint8_t)iv_len;

	output[iv_len+in_len]^=output[0];
	output[iv_len+in_len]^=key_string[0];

	for(i=0,j=0,k=1;i<in_len;i++,j++,k++)
	{
		if(j==iv_len) j=0;
		if(key_string[k]==0)k=0;
		output[iv_len+i]^=output[j];
		output[iv_len+i]^=key_string[k];
	}


	out_len=iv_len+in_len+1;
	return 0;
}
int de_obscure(const char * input, int in_len,char *output,int &out_len)
{
	//memcpy(output,input,in_len);
	//out_len=in_len;
	//return 0;

	int i, j, k;
	if (in_len > 65535||in_len<0)
	{
		mylog(log_debug,"in_len > 65535||in_len<0 ,  %d",in_len);
		return -1;
	}
	int iv_len= int ((uint8_t)(input[in_len-1]^input[0]^key_string[0]) );
	out_len=in_len-1-iv_len;
	if(out_len<0)
	{
		mylog(log_debug,"%d %d\n",in_len,out_len);
		return -1;
	}
	for(i=0,j=0,k=1;i<in_len;i++,j++,k++)
	{
		if(j==iv_len) j=0;
		if(key_string[k]==0)k=0;
		output[i]=input[iv_len+i]^input[j]^key_string[k];

	}
	dup_packet_recv_count++;
	return 0;
}


int sendto_fd_ip_port (int fd,u32_t ip,int port,char * buf, int len,int flags)
{

	sockaddr_in tmp_sockaddr;

	memset(&tmp_sockaddr,0,sizeof(tmp_sockaddr));
	tmp_sockaddr.sin_family = AF_INET;
	tmp_sockaddr.sin_addr.s_addr = ip;
	tmp_sockaddr.sin_port = htons(uint16_t(port));

	return sendto(fd, buf,
			len , 0,
			(struct sockaddr *) &tmp_sockaddr,
			sizeof(tmp_sockaddr));
}
int sendto_ip_port (u32_t ip,int port,char * buf, int len,int flags)
{
	return sendto_fd_ip_port(local_listen_fd,ip,port,buf,len,flags);
}
int send_fd (int fd,char * buf, int len,int flags)
{
	/*
	if(is_client)
	{
		dup_packet_send_count++;
	}
	if(is_client&&random_drop!=0)
	{
		if(get_true_random_number()%10000<(u32_t)random_drop)
		{
			return 0;
		}
	}*/
	return send(fd,buf,len,flags);
}
//enum delay_type_t {none=0,enum_sendto_u64,enum_send_fd,client_to_local,client_to_remote,server_to_local,server_to_remote};

int my_send(const dest_t &dest,char *data,int len)
{
	switch(dest.type)
	{
		case type_ip_port:
		{
			return sendto_ip_port(dest.inner.ip_port.ip,dest.inner.ip_port.port,data,len,0);
			break;
		}
		case type_ip_port_conv:
		{
			char *new_data;
			int new_len;
			put_conv(dest.conv,data,len,new_data,new_len);
			return sendto_ip_port(dest.inner.ip_port.ip,dest.inner.ip_port.port,new_data,new_len,0);
			break;
		}
		case type_fd64:
		{
			if(!fd_manager.exist(dest.inner.fd64)) return -1;
			int fd=fd_manager.to_fd(dest.inner.fd64);
			return send_fd(fd,data,len,0);
			break;
		}
		case type_fd64_conv:
		{
			char *new_data;
			int new_len;
			put_conv(dest.conv,data,len,new_data,new_len);

			if(!fd_manager.exist(dest.inner.fd64)) return -1;
			int fd=fd_manager.to_fd(dest.inner.fd64);
			return send_fd(fd,new_data,new_len,0);
		}
		/*
		case type_fd:
		{
			send_fd(dest.inner.fd,data,len,0);
			break;
		}*/
		default:
			assert(0==1);
	}
	return 0;
}

/*
 *  this function comes from  http://www.hackersdelight.org/hdcodetxt/crc.c.txt
 */
unsigned int crc32h(unsigned char *message,int len) {
   int i, crc;
   unsigned int byte, c;
   const unsigned int g0 = 0xEDB88320,    g1 = g0>>1,
      g2 = g0>>2, g3 = g0>>3, g4 = g0>>4, g5 = g0>>5,
      g6 = (g0>>6)^g0, g7 = ((g0>>6)^g0)>>1;

   i = 0;
   crc = 0xFFFFFFFF;
   while (i!=len) {    // Get next byte.
	   byte = message[i];
      crc = crc ^ byte;
      c = ((crc<<31>>31) & g7) ^ ((crc<<30>>31) & g6) ^
          ((crc<<29>>31) & g5) ^ ((crc<<28>>31) & g4) ^
          ((crc<<27>>31) & g3) ^ ((crc<<26>>31) & g2) ^
          ((crc<<25>>31) & g1) ^ ((crc<<24>>31) & g0);
      crc = ((unsigned)crc >> 8) ^ c;
      i = i + 1;
   }
   return ~crc;
}

int put_conv(u32_t conv,const char * input,int len_in,char *&output,int &len_out)
{
	static char buf[buf_len];
	output=buf;
	u32_t n_conv=htonl(conv);
	memcpy(output,&n_conv,sizeof(n_conv));
	memcpy(output+sizeof(n_conv),input,len_in);
	u32_t crc32=crc32h((unsigned char *)output,len_in+sizeof(crc32));
	u32_t crc32_n=htonl(crc32);
	len_out=len_in+(int)(sizeof(n_conv))+(int)sizeof(crc32_n);
	memcpy(output+len_in+(int)(sizeof(n_conv)),&crc32_n,sizeof(crc32_n));
	return 0;
}
int get_conv(u32_t &conv,const char *input,int len_in,char *&output,int &len_out )
{
	u32_t n_conv;
	memcpy(&n_conv,input,sizeof(n_conv));
	conv=ntohl(n_conv);
	output=(char *)input+sizeof(n_conv);
	u32_t crc32_n;
	len_out=len_in-(int)sizeof(n_conv)-(int)sizeof(crc32_n);
	if(len_out<0)
	{
		mylog(log_debug,"len_out<0\n");
		return -1;
	}
	memcpy(&crc32_n,input+len_in-(int)sizeof(crc32_n),sizeof(crc32_n));
	u32_t crc32=ntohl(crc32_n);
	if(crc32!=crc32h((unsigned char *)input,len_in-(int)sizeof(crc32_n)))
	{
		mylog(log_debug,"crc32 check failed\n");
		return -1;
	}
	return 0;
}