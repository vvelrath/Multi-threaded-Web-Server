#define	BUF_LEN	8192

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include	<sys/stat.h>
#include	<netdb.h>
#include	<netinet/in.h>
#include	<inttypes.h>
#include	<time.h>
#include    <pthread.h>
#include    <dirent.h>
#include    <pwd.h>
#define _GNU_SOURCE

char *progname;
char buf[BUF_LEN];

struct queue
{
int acceptfd;
char filename[1024];
long long int filesize;
char *arrivaltime;
char *exectime;
char *firstline;
int status;
struct queue *link;
};

struct queue *front=NULL,*rear = NULL;
struct queue *node_to_serve;
struct queue *temp=NULL,*temp_previous=NULL;

pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond_schedule  = PTHREAD_COND_INITIALIZER;
pthread_cond_t  cond_serve  = PTHREAD_COND_INITIALIZER;

const char *root=NULL;
const char *logfile = NULL;
int logging=0;

void usage();
int setup_client();
int setup_server();

int s, sock, ch, server, done, bytes, aflg;
int soctype = SOCK_STREAM;
char *host = NULL;
char *port = NULL;
int sch=0;

extern char *optarg;
extern int optind;
extern int opterr;

//Scheduling thread inserts the incoming request into the waiting queue
void insertion(int newsock,char *filename,long long int filesize,char *arrivaltime,char *firstline)
{
	struct queue *newnode;
	
	newnode=(struct queue *)malloc(sizeof(struct queue));
	
	if(newnode==NULL)
	{
	printf("No Memory available Error\n");
	exit(0);
	}
	
	char a[1024];
	strcpy(a,filename);
	
	newnode->acceptfd=newsock;
	
	strcpy(newnode->filename,a);
	   
	newnode->filesize=filesize;
	newnode->arrivaltime=arrivaltime;
	newnode->firstline=firstline;
	newnode->link=NULL;
	
	pthread_mutex_lock(&qmutex);
	
	if(sch==0)
	{
		if(front==NULL)
		{
		rear=newnode;
		front=rear;
		}
		else
		{
		rear->link=newnode;
		rear=newnode;
		}
	}
	else
	{
		if(front==NULL)
		{
			rear=newnode;
			front=rear;
		}
		else
		{
			if(newnode->filesize<=front->filesize)
			{
			newnode->link=front;
			front=newnode;
			}
			else if(newnode->filesize>rear->filesize)
			{
				rear->link=newnode;
				rear=newnode;
			}
			else
			{
				temp=front;
				while(temp!=NULL)
				{
					if(newnode->filesize<=temp->filesize)
					{
						temp_previous->link=newnode;
						newnode->link=temp;
						break;
					}
				temp_previous = temp;	
				temp=temp->link;
				}
			}	
		}
	}	
	
	pthread_mutex_unlock(&qmutex);
	
	pthread_cond_signal(&cond_schedule);
}

//The worker threads removes the request from the queue for processing
void *scheduler(void* args)
{
	while(1)
	{
	
	pthread_mutex_lock(&qmutex);
	
	if(front==NULL)
	{
		pthread_cond_wait(&cond_schedule,&qmutex);
		pthread_mutex_unlock(&qmutex);
		continue;
	}
	else
	{
		if(front!=NULL && front==rear)
		{
			node_to_serve=front;
			front=NULL;
			rear=NULL;
		}
		
		if(front!=NULL && front!=rear)
		{
			node_to_serve=front;
			front=front->link;
		}
		
	pthread_mutex_unlock(&qmutex);
	pthread_cond_signal(&cond_serve);	
	sleep(1);
	}
	
	}
}

//The worker threads use the request handler function to service the request
void *request_handler(void* args)
{
		while(1)
		{
		pthread_mutex_lock(&qmutex);
		pthread_cond_wait(&cond_serve,&qmutex);
		
		char filename[1024];
		int socketid;
		FILE*  fp1;
		int    ret;
		size_t read;
		struct stat filestat;
		char*  readBuf = NULL;
		
		strcpy(filename,node_to_serve->filename);
		socketid=node_to_serve->acceptfd;
		
		time_t now = time(NULL);
		
		node_to_serve->exectime=asctime(gmtime((const time_t*)&now));
		
		ret = stat(filename, &filestat);
	 
		readBuf = (char*) malloc( sizeof(char));
	 			
		char buf_tmp1[BUF_LEN];
		strcpy(buf_tmp1,node_to_serve->firstline);
		
		char firstline[200];
					
		int j=0;
		int i=0;
		for(i=0;i<strlen(buf_tmp1);i++)
		{	
			if(buf_tmp1[i]=='\n')
			{
			firstline[j]='\0';
			break;
			}
			firstline[j++] = buf_tmp1[i];
		}
		
		
		strtok(buf_tmp1," ");
		char *remote_add=malloc(sizeof(char *));
		remote_add=	strtok(NULL, " ");
		remote_add=	strtok(NULL, " ");
		remote_add=	strtok(NULL, " ");
		char remote[256],remote_tmp[256];
		strcpy(remote,remote_add);
		
		i=0,j=0;
		for(i=0;i<strlen(remote);i++)
		{	
			if(remote[i]==':')
			{
			remote_tmp[j]='\0';
			break;
			}
			remote_tmp[j++] = remote[i];
		}
		
		char log_txt[1024];
		char log_txt1[1024];
		strcpy(log_txt,remote_tmp);
		strcat(log_txt,"  -  [");
		strcat(log_txt,node_to_serve->arrivaltime);
		strcat(log_txt,"]  [");
		strcat(log_txt,node_to_serve->exectime);
		strcat(log_txt,"]  ");
		strcat(log_txt,firstline);
		
		char stringNum1[10];
		char stringNum2[10];
		
		char *stringNump1=malloc(sizeof(char*));
		char *stringNump2=malloc(sizeof(char*));
		
		int status;
		fp1 = fopen( filename, "r" );
		if(fp1==NULL)
			status=404;
		else
			status=200;
			
		snprintf(stringNum1,10,"%d",status);
		snprintf(stringNum2,10,"%lld",node_to_serve->filesize);
		
		strcpy(stringNump1,stringNum1);
		strcpy(stringNump2,stringNum2);
		
		strcat(log_txt,"  ");
		strcat(log_txt,stringNump1);
		strcat(log_txt,"  ");
		strcat(log_txt,stringNump2);
		
		
		i=0,j=0;
		for(i=0;i<strlen(log_txt);i++)
		{	
			if(log_txt[i]=='\n')
				log_txt1[j++] = ' ';
			else if(log_txt[i]=='\r')
				log_txt1[j++] = ' ';	
			else
				log_txt1[j++] = log_txt[i];
		}
		
		if(logging==1)
		{
			char logfile_full[1024];
			char *slash=strstr(logfile,"/");
			if(slash==NULL)
			{
				strcpy(logfile_full,root);
				strcat(logfile_full,"/");
				strcat(logfile_full,logfile);
			}
			else
				strcpy(logfile_full,logfile);	
			FILE *fp_log;
			fp_log = fopen(logfile_full, "a");
			fprintf(fp_log,"%s\n",log_txt1);
			fclose(fp_log);
		}
			
		/*Code Adapted from http://www.cplusplus.com/forum/unices/16005/ starts here*/
		DIR *directory=NULL;
		struct dirent *drnt = NULL;
		directory=opendir(filename);
		struct stat buf1;
		char filesize_dir_string[10];
		char *dname=(char*)malloc(sizeof(char*));
		
		/*Code Adapted from http://www.cplusplus.com/forum/unices/16005/ ends here*/
		
		if(strcmp(buf_tmp1,"HEAD")==0)
		{
			char response[2048];
			struct stat buf_head;
			time_t now = time(NULL);
			
			ret = stat((const char*) filename, &buf_head);
			char *time_head = asctime(gmtime(&(buf_head.st_mtime)));
			
			
			char *time_now  =  asctime(gmtime((const time_t*)&now));
					
			strcpy(response,"HTTP/1.1 200 OK\nDate: ");
			strcat(response,time_now);
			strcat(response,"\nServer: Apache/2.2.3\nLast-Modified: ");
			strcat(response,time_head);
			strcat(response,"\nContent-Type: text/html\n");
			strcat(response,"Content-Length: 0\n");
			
			send(socketid,response,strlen(response),0);
		}
		else if(directory)
		{
			char index[2048];
			char response[1024];
			char indexhtml_path[1024];
			strcpy(indexhtml_path,filename);
			if(filename[strlen(filename)-1]=='/')
				strcat(indexhtml_path,"index.html");
			else
				strcat(indexhtml_path,"/index.html");
				
			FILE *fpindexhtml = NULL;
			fpindexhtml = fopen(indexhtml_path,"r");
			char *readBufindex = (char*) malloc( sizeof(char));
			if(fpindexhtml)
			{
				strcpy(response,"HTTP/1.0 200 OK\nContent-Type:text/html\n\n");

				send(socketid, response, strlen(response), 0);
			
				while (0 < (read = fread( (void*) readBufindex, sizeof(char), 1, fpindexhtml)))
				{
					readBufindex[read]='\0';
					send(socketid, readBufindex, strlen(readBufindex), 0);
				}
				fclose(fpindexhtml);
			}
			else
			{
			char readDir[2048];
			char filepath_dir[2048];
			strcpy(readDir,"");
			
			fprintf(stderr,"FILENAME:%s",filename);
			strcpy(index,"HTTP/1.0 200 OK\nContent-Type:text/html\n\n");
			strcat(index,"<html><head><h3>Index of ");
			strcat(index,filename);
			strcat(index,"</h3></head>");
			strcat(index,"<body><table border=1><tr><th>File Name</th><th>Last Modified Date</th><th>Size</th></tr>");
			
			send(socketid, index,strlen(index), 0);
			
			while(drnt=readdir(directory))
			{
			const char *dname=drnt->d_name;
			
			if(dname[0]!='.')
			{
				strcat(readDir,"<tr><td>");
				strcat(readDir,drnt->d_name);
				strcat(readDir,"</td><td>");
							
				i=0,j=0;
				
				strcpy(filepath_dir,filename);
				//strcat(filepath_dir,"/");
				strcat(filepath_dir,dname);
				
				ret = stat((const char*) filepath_dir, &buf1);
				
				char time[strlen(asctime(gmtime(&(buf1.st_mtime))))];
				for(i=0;i<strlen(asctime(gmtime(&(buf1.st_mtime))));i++)
				{	
					if(asctime(gmtime(&(buf1.st_mtime)))[i]=='\n'||asctime(gmtime(&(buf1.st_mtime)))[i]=='\r')
						time[j++]=' ';
					else
					{
						time[j++]=asctime(gmtime(&(buf1.st_mtime)))[i];
					}
				}
				time[j]='\0';
				strcat(readDir,time);
				strcat(readDir,"</td><td>");
				long long int filesize_dir=buf1.st_size;
				snprintf(filesize_dir_string,10,"%lld",filesize_dir);
				strcat(readDir,filesize_dir_string);
				strcat(readDir,"</td></tr>");
			}
			}
			
			strcat(readDir,"</table><body></html>");
			send(socketid, readDir,strlen(readDir), 0);
			close(directory);
			}
		}
		else
		{
			int read;
			char readBuf[2];
			char filenotfound[1024];
			char response[1024];
			
			if(fp1)
			{
				strcpy(response,"HTTP/1.1 200 OK");
							
				char *str1 = strstr(filename,".txt");
				char *str2 = strstr(filename,".html");
				char *str3 = strstr(filename,".gif");
				char *str4 = strstr(filename,".jpg");
				char *str5 = strstr(filename,".png");
				
				if(str1!=NULL ||str2!=NULL)
					strcat(response,"\nContent-Type:text/html\n\n");
				if(str3!=NULL)
					strcat(response,"\nContent-Type:image/gif\n\n");
				if(str4!=NULL)
					strcat(response,"\nContent-Type:image/jpeg\n\n");
				if(str5!=NULL)
					strcat(response,"\nContent-Type:image/png\n\n");
				

				send(socketid, response, strlen(response), 0);
				
				while (0 < (read = fread( (void*) readBuf, sizeof(char), 1, fp1 )))
				{
					send(socketid, readBuf, strlen(readBuf), 0);
				}
				fclose(fp1);
			}
			else
			{
			char index[1024];
			strcpy(index,"HTTP/1.0 404 Not Found\nContent-Type:text/html\n\n");
			strcat(index,"<html><head>FILE NOT FOUND</head></html>");
			send(socketid,index,strlen(index),0);
			}
		}
		
		close(socketid);
		free(readBuf);
		
		pthread_mutex_unlock(&qmutex);
		sleep(1);
		}
}

//The listener listens for incoming requests and inserts it into the queue	
void *listener(void* args)
{	
	if ((s = socket(AF_INET, soctype, 0)) < 0) 
	{
		perror("socket");
		exit(1);
	}
	
	struct sockaddr_in serv, remote;
	struct servent *se;
	int newsock, len;
	
	fd_set ready;
	struct sockaddr_in msgfrom;
	int msgsize;
	union 
	{
		uint32_t addr;
		char bytes[4];
	} fromaddr;
	
	len = sizeof(remote);
	memset((void *)&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;

	if (port == NULL)
		serv.sin_port = htons(8080);
	else if (isdigit(*port))
	{
		serv.sin_port = htons(atoi(port));
	}
	else 
	{
		if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) 
		{
			perror(port);
			exit(1);
		}
		serv.sin_port = se->s_port;
	}
	
	if (bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) 
	{
		perror("bind");
		exit(1);
	}
		
	if (getsockname(s, (struct sockaddr *) &remote, &len) < 0) 
	{
		perror("getsockname");
		exit(1);
	}
	
	fprintf(stderr, "Port number is %d\n", ntohs(remote.sin_port));
	
	listen(s, 1);
	
	daemon(0,0);
		
	while(1)
	{
		
	sleep(1);
	
	newsock = s;
	if (soctype == SOCK_STREAM) 
	{
	fprintf(stderr, "Entering accept() waiting for connection.\n");
	newsock = accept(s, (struct sockaddr *) &remote, &len);
	}
		
	FD_ZERO(&ready);
	FD_SET(newsock, &ready);
	
	if (select((newsock + 1), &ready, 0, 0, 0) < 0) {
		perror("select");
		exit(1);
	}
	
	if (FD_ISSET(fileno(stdin), &ready)) {
		if ((bytes = read(fileno(stdin), buf, BUF_LEN)) <= 0)
			done=0;
		send(newsock, buf, bytes, 0);
	}
	msgsize = sizeof(msgfrom);
	
	if (FD_ISSET(newsock, &ready)) 
	{
		if ((bytes = recvfrom(newsock, buf, BUF_LEN, 0, (struct sockaddr *)&msgfrom, &msgsize)) <= 0) 
		{
			done=0;
		} 
		else if (aflg) 
		{
			fromaddr.addr = ntohl(msgfrom.sin_addr.s_addr);
			fprintf(stderr, "%d.%d.%d.%d: ", 0xff & (unsigned int)fromaddr.bytes[0],
				0xff & (unsigned int)fromaddr.bytes[1],
				0xff & (unsigned int)fromaddr.bytes[2],
				0xff & (unsigned int)fromaddr.bytes[3]);		
		}
			
		write(fileno(stdout), buf, bytes);
			
		char filename_full[200];
		
		char *buf_tmp=malloc(sizeof(buf));
		strcpy(buf_tmp,buf);
		
		char *filename_t=malloc(sizeof(char *));
		strtok(buf, " "); 						
        filename_t = strtok(NULL, " ");			
		
		int ret=0;
		
		struct stat buf;
		
		strcpy(filename_full,root);
							
		strcat(filename_full,filename_t);
		
		
		char file_tilda[1024];
		char file_tilda_full_path[1024];
		char filename_tilda[1024];
		strcpy(file_tilda,filename_t);
				
		if(file_tilda[1]=='~')
		{
			char username[100];
			
			int i=0;
			int j=0;
			int k=0;
			for(i=2;i<strlen(file_tilda);i++)
			{
				if(file_tilda[i]=='/')
				{
				username[j]='\0';
				break;
				}
				username[j++]=file_tilda[i];
			}
			
			j=0;
			for(k=i;k<strlen(file_tilda);k++)
			{
				filename_tilda[j++]=file_tilda[k];		
			}
			filename_tilda[j]='\0';
			
			
			struct passwd *homedir;
			homedir = getpwnam(username);
					
				
			strcpy(file_tilda_full_path,homedir->pw_dir);
			strcat(file_tilda_full_path,"/myhttpd");
			strcat(file_tilda_full_path,filename_tilda);
			
			strcpy(filename_full,file_tilda_full_path);
		}
				
		ret = stat((const char*) filename_full, &buf);
		
		time_t now = time(NULL);
		
		insertion(newsock,filename_full,(long long) buf.st_size,asctime(gmtime((const time_t*)&now)),buf_tmp);
			
	}
		
	fprintf(stderr, "Connection Accepted.\n");
	}
}


//Main function
int main(int argc,char *argv[])
{
	int new=0;
	printf("welcome");
	int debug = 0;
    int n_threads = 4;
    int queueing_time = 60;
    char *schedPolicy = "FCFS";
    int c;
    opterr = 0;
 	
	if ((progname = rindex(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;
		
	char rootdir[1024];
	getcwd(rootdir,1024);	
	
	
	while ((ch = getopt(argc, argv, "dhl:p:r:t:n:s:")) != -1)
		switch(ch) {
			case 'a':
				aflg++;		/* print address in output */
				break;
			case 'd':
                debug = 1;
                break;
            case 'h':
                usage();
                exit(1);
            case 'l':
				logging=1;
                logfile = optarg;
                break;
			case 's':
                schedPolicy = optarg;
				if(schedPolicy=="FCFS")
					sch=0;
				else	
					sch=1;
                break;
			case 'p':
				port = optarg;
				if (atoi(port) < 1024)
                {
                    fprintf(stderr, "[error] Port number must be greater than or equal to 1024.\n");
                    exit(1);
                }
                break;
			case 'r':
                root = optarg;
                break;
            case 't':
                queueing_time = atoi(optarg);
                if (queueing_time < 1)
                {
                        fprintf(stderr, "[error] queueing time must be greater than 0.\n");
                        exit(1);
                }
                break;
            case 'n':
                n_threads = atoi(optarg);
                if (n_threads < 1)
                {
                        fprintf(stderr, "[error] number of threads must be greater than 0.\n");
                        exit(1);
                }
                break;	
			case '?':
			default:
				usage();
				exit(1);
		}
	
	if(root==NULL)
		root=rootdir;
		

	if (debug == 1)
    {
        if(logfile==NULL) 
			fprintf(stderr, "myhttpd logfile: \n", logfile);
		else
			fprintf(stderr, "myhttpd logfile: %s \n", logfile);
        if(port==NULL) 
			fprintf(stderr, "myhttpd port number: 8080\n", port);
		else 
			fprintf(stderr, "myhttpd port number: %d\n", atoi(port));
        fprintf(stderr, "myhttpd rootdir: %s\n", root);
        fprintf(stderr, "myhttpd queueing time: %d\n", queueing_time);
        fprintf(stderr, "myhttpd number of threads: %d\n", n_threads);
        fprintf(stderr, "myhttpd scheduling policy: %s\n", schedPolicy);
		
		if ((s = socket(AF_INET, soctype, 0)) < 0) 
		{
		perror("socket");
		exit(1);
		}
	
		struct sockaddr_in serv, remote;
		struct servent *se;
		int newsock, len;
		fd_set ready;
		struct sockaddr_in msgfrom;
		int msgsize;
		union 
		{
			uint32_t addr;
			char bytes[4];
		} fromaddr;
	
		len = sizeof(remote);
		memset((void *)&serv, 0, sizeof(serv));
		serv.sin_family = AF_INET;
		
		if (port == NULL)
			serv.sin_port = htons(8080);
		else if (isdigit(*port))
		{
			serv.sin_port = htons(atoi(port));
		}
		else 
		{
			if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) 
			{
				perror(port);
				exit(1);
			}
			serv.sin_port = se->s_port;
		}
		
		if (bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) 
		{
			perror("bind");
			exit(1);
		}
			
		if (getsockname(s, (struct sockaddr *) &remote, &len) < 0) 
		{
			perror("getsockname");
			exit(1);
		}
		
		fprintf(stderr, "Port number is %d\n", ntohs(remote.sin_port));
			
		listen(s, 1);
		
		while(1)
		{
			newsock = s;
			if (soctype == SOCK_STREAM) 
			{
			fprintf(stderr, "Entering accept() waiting for connection.\n");
			newsock = accept(s, (struct sockaddr *) &remote, &len);
			}
				
			FD_ZERO(&ready);
			FD_SET(newsock, &ready);
			
			if (select((newsock + 1), &ready, 0, 0, 0) < 0) {
				perror("select");
				exit(1);
			}
				
			if (FD_ISSET(fileno(stdin), &ready)) {
				if ((bytes = read(fileno(stdin), buf, BUF_LEN)) <= 0)
					done=0;
				send(newsock, buf, bytes, 0);
			}
			msgsize = sizeof(msgfrom);
			
			if (FD_ISSET(newsock, &ready)) 
			{
				if ((bytes = recvfrom(newsock, buf, BUF_LEN, 0, (struct sockaddr *)&msgfrom, &msgsize)) <= 0) 
				{
					done=0;
				} 
				else if (aflg) 
				{
					fromaddr.addr = ntohl(msgfrom.sin_addr.s_addr);
					fprintf(stderr, "%d.%d.%d.%d: ", 0xff & (unsigned int)fromaddr.bytes[0],
						0xff & (unsigned int)fromaddr.bytes[1],
						0xff & (unsigned int)fromaddr.bytes[2],
						0xff & (unsigned int)fromaddr.bytes[3]);
				}
					
				write(fileno(stdout), buf, bytes);
					
				char filename_full[200];
				
				char *buf_tmp=malloc(sizeof(buf));
				strcpy(buf_tmp,buf);
				
				
				
				char firstline[200];
					
				int j=0;
				int i=0;
				
				for(i=0;i<strlen(buf);i++)
				{	
				if(buf[i]=='\n')
				{
				firstline[j]='\0';
				break;
				}
				firstline[j++] = buf[i];
				}
				
				char *filename_t=malloc(sizeof(char *));
				strtok(buf, " ");
				char buf2[1024];
				strcpy(buf2,buf);	
				filename_t = strtok(NULL, " ");			

				int ret=0;
						
				strcpy(filename_full,root);
									
				strcat(filename_full,filename_t);
				
				char file_tilda[1024];
				char file_tilda_full_path[1024];
				char filename_tilda[1024];
				char username[100];
				strcpy(file_tilda,filename_t);
						
				if(file_tilda[1]=='~')
				{
					int i=0;
					int j=0;
					int k=0;
					
					for(i=2;i<strlen(file_tilda);i++)
					{
						if(file_tilda[i]=='/')
						{
						username[j]='\0';
						break;
						}
						username[j++]=file_tilda[i];
					}
					
					j=0;
					for(k=i;k<strlen(file_tilda);k++)
					{
						filename_tilda[j++]=file_tilda[k];		
					}
					filename_tilda[j]='\0';
					
					struct passwd *homedir;
					homedir = getpwnam(username);
					
					strcpy(file_tilda_full_path,homedir->pw_dir);
					strcat(file_tilda_full_path,"/myhttpd");
					strcat(file_tilda_full_path,filename_tilda);
					
					strcpy(filename_full,file_tilda_full_path);
		
					username[0]='\0';
				}
				
				
				char *remote_add = malloc(sizeof(char *));
				remote_add=	strtok(NULL, " ");
				remote_add=	strtok(NULL, " ");

				char remote[256],remote_tmp[256];
				strcpy(remote,remote_add);

				i=0,j=0;
				for(i=0;i<strlen(remote);i++)
				{	
				if(remote[i]==':')
				{
				remote_tmp[j]='\0';
				break;
				}
				remote_tmp[j++] = remote[i];
				}
				
				struct stat buf;
				ret = stat((const char*) filename_full, &buf);

				time_t now = time(NULL);
				
				char log_txt[1024];
				char log_txt1[1024];
				strcpy(log_txt,remote_tmp);
				strcat(log_txt,"  -  [");
				strcat(log_txt,asctime(gmtime((const time_t*)&now)));
				strcat(log_txt,"]  [");
				strcat(log_txt,asctime(gmtime((const time_t*)&now)));
				strcat(log_txt,"]  ");
				strcat(log_txt,firstline);

				char stringNum1[10];
				char stringNum2[10];
				
				FILE *fp1 = fopen(filename_full, "r");
				int status;
				if(fp1==NULL)
					status=404;
				else
					status=200;
				
				
				char *stringNump1=malloc(sizeof(char*));
				char *stringNump2=malloc(sizeof(char*));
					
				snprintf(stringNum1,10,"%d",status);
				
				snprintf(stringNum2,10,"%lld",(long long int) buf.st_size);
				
				strcpy(stringNump1,stringNum1);
				strcpy(stringNump2,stringNum2);
				
				strcat(log_txt,"  ");
				strcat(log_txt,stringNump1);
				strcat(log_txt,"  ");
				strcat(log_txt,stringNump2);
				

				i=0,j=0;
				for(i=0;i<strlen(log_txt);i++)
				{	
				if(log_txt[i]=='\n')
				log_txt1[j++] = ' ';
				else if(log_txt[i]=='\r')
				log_txt1[j++] = ' ';	
				else
				log_txt1[j++] = log_txt[i];
				}

				/*Code Adapted from http://www.cplusplus.com/forum/unices/16005/ starts here*/
				DIR *directory=NULL;
				struct dirent *drnt = NULL;
				directory=opendir(filename_full);
				struct stat buf1;
				char filesize_dir_string[10];
				char *dname=(char*)malloc(sizeof(char*));
				
				/*Code Adapted from http://www.cplusplus.com/forum/unices/16005/ ends here*/
				
				if(strcmp(buf2,"HEAD")==0)
				{
					char response[2048];
					struct stat buf_head;
					time_t now = time(NULL);
					
					ret = stat((const char*) filename_full, &buf_head);
					char *time_head = asctime(gmtime(&(buf_head.st_mtime)));
					
					
					char *time_now  =  asctime(gmtime((const time_t*)&now));
							
					strcpy(response,"HTTP/1.1 200 OK\nDate: ");
					strcat(response,time_now);
					strcat(response,"\nServer: Apache/2.2.3\nLast-Modified: ");
					strcat(response,time_head);
					strcat(response,"\nContent-Type: text/html\n");
					strcat(response,"Content-Length: 0\n");
					
					send(newsock,response,strlen(response),0);
				}
				else if(directory)
				{
					char index[2048];
					char response[1024];
					char indexhtml_path[1024];
					strcpy(indexhtml_path,filename_full);
					if(filename_full[strlen(filename_full)-1]=='/')
						strcat(indexhtml_path,"index.html");
					else
						strcat(indexhtml_path,"/index.html");
						
					FILE *fpindexhtml = NULL;
					fpindexhtml = fopen(indexhtml_path,"r");
					char *readBufindex = (char*) malloc( sizeof(char));
					
					

					if(fpindexhtml)
					{
						strcpy(response,"HTTP/1.0 200 OK\nContent-Type:text/html\n\n");
											
						send(newsock, response, strlen(response), 0);
						int read;
						while (0 < (read = fread( (void*) readBufindex, sizeof(char), 1, fpindexhtml)))
						{
							readBufindex[read]='\0';
							send(newsock, readBufindex, strlen(readBufindex), 0);
						}
						fclose(fpindexhtml);
					}
					else
					{
					char readDir[2048];
					char filepath_dir[2048];
					strcpy(readDir,"");
					
					strcpy(index,"HTTP/1.0 200 OK\nContent-Type:text/html\n\n");
					strcat(index,"<html><head><h3>Index of ");
					strcat(index,filename_full);
					strcat(index,"</h3></head>");
					strcat(index,"<body><table border=1><tr><th>File Name</th><th>Last Modified Date</th><th>Size</th></tr>");
					
					send(newsock, index,strlen(index), 0);
					while(drnt=readdir(directory))
					{
					const char *dname=drnt->d_name;
					
					if(dname[0]!='.')
					{
						strcat(readDir,"<tr><td>");
						strcat(readDir,drnt->d_name);
						strcat(readDir,"</td><td>");
									
						i=0,j=0;
						
						strcpy(filepath_dir,filename_full);
						strcat(filepath_dir,"/");
						strcat(filepath_dir,dname);
						
						ret = stat((const char*) filepath_dir, &buf1);
						
						char time[strlen(asctime(gmtime(&(buf1.st_mtime))))];
						for(i=0;i<strlen(asctime(gmtime(&(buf1.st_mtime))));i++)
						{	
							if(asctime(gmtime(&(buf1.st_mtime)))[i]=='\n'||asctime(gmtime(&(buf1.st_mtime)))[i]=='\r')
								time[j++]=' ';
							else
							{
								time[j++]=asctime(gmtime(&(buf1.st_mtime)))[i];
							}
						}
						time[j]='\0';
						strcat(readDir,time);
						strcat(readDir,"</td><td>");
						long long int filesize_dir=buf1.st_size;
						snprintf(filesize_dir_string,10,"%lld",filesize_dir);
						strcat(readDir,filesize_dir_string);
						strcat(readDir,"</td></tr>");
					}
					}
					
					strcat(readDir,"</table><body></html>");
					send(newsock, readDir,strlen(readDir), 0);
					close(directory);
					}
				}
				else
				{
					int read;
					char readBuf[2];
					char filenotfound[1024];
					char response[1024];
					
					if(fp1)
					{
					strcpy(response,"HTTP/1.1 200 OK");
					
					
					char *str1 = strstr(filename_full,".txt");
					char *str2 = strstr(filename_full,".html");
					char *str3 = strstr(filename_full,".gif");
					char *str4 = strstr(filename_full,".jpg");
					char *str5 = strstr(filename_full,".png");

					if(str1!=NULL ||str2!=NULL)
						strcat(response,"\nContent-Type: text/html\n\n");
					if(str3!=NULL)
						strcat(response,"\nContent-Type: image/gif\n\n");
					if(str4!=NULL)
						strcat(response,"\nContent-Type: image/jpeg\n\n");
					if(str5!=NULL)
						strcat(response,"\nContent-Type: image/png\n\n");
					
					
					send(newsock, response, strlen(response), 0);
					
						while (0 < (read = fread( (void*) readBuf, sizeof(char), 1, fp1 )))
						{
							readBuf[read]='\0';
							send(newsock, readBuf, strlen(readBuf), 0);
						}
						fclose(fp1);
					}
					else
					{
					strcpy(filenotfound,"HTTP/1.1 404 Not Found\n");
					strcat(filenotfound,"<html><head>FILE NOT FOUND</head></html>");
					send(newsock,filenotfound,strlen(filenotfound),0);
					}
					
				}
				
				close(newsock);				
			}
			
			fprintf(stderr, "\nConnection Accepted.\n");
		}
		
		exit(0);
    }
	else
	{
		pthread_t worker_threads[4];
		
		int i;
		for (i=0; i<4; i++) 
		{
		pthread_create(&worker_threads[i], NULL, &request_handler, NULL);
		}
		
		pthread_t listening_thread;
		pthread_create(&listening_thread,NULL,&listener,NULL);
		
		sleep(queueing_time);
		
		pthread_t scheduling_thread;
		pthread_create(&scheduling_thread,NULL,&scheduler,NULL);
		
		pthread_join(listening_thread,NULL);
		pthread_join(scheduling_thread,NULL);
		
		for (i=0; i<4; i++) 
		{
		pthread_join(worker_threads[i], NULL);
		}
	}
	return(0);
}

//Prints the usage of the commands
void usage()
{
	fprintf(stderr, "Usage: myhttpd [−d] [−h] [−l file] [−p port] [−r dir] [−t time] [−n thread_num]  [−s sched]\n");
 
    fprintf(stderr,
            "\t−d : Enter debugging mode. That is, do not daemonize, only accept\n"
            "\tone connection at a time and enable logging to stdout.  Without\n"
            "\tthis option, the web server should run as a daemon process in the\n"
            "\tbackground.\n"
            "\t−h : Print a usage summary with all options and exit.\n"
            "\t−l file : Log all requests to the given file. See LOGGING for\n"
            "\tdetails.\n"
            "\t−p port : Listen on the given port. If not provided, myhttpd will\n"
            "\tlisten on port 8080.\n"
            "\t−r dir : Set the root directory for the http server to dir.\n"
            "\t−t time : Set the queuing time to time seconds.  The default should\n"
            "\tbe 60 seconds.\n"
            "\t−n thread_num : Set number of threads waiting ready in the execution thread pool to\n"
            "\tthreadnum.  The d efault should be 4 execution threads.\n"
            "\t−s sched : Set the scheduling policy. It can be either FCFS or SJF.\n"
            "\tThe default will be FCFS.\n"
            );
	exit(1);
}