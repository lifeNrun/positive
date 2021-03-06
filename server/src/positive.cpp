#include "positiveHttp.h"
/*
*1. 文件监测，看看有没有新的文件加入，或者文件内容修改了，如果有，那么就放入内存池里面
*2. 反向代理的实现
*3. 对于fastcgi的支持
*4. 从配置文件获取到对应的配置
*/
#define  POOL_SORT 0
#define  THREAD_EPOLL 1
using namespace std;
pthread_mutex_t mutex;
void printLink(positive_pool_t *head);
PositiveServer::PositiveServer()
{
}

PositiveServer::~PositiveServer()
{
    close(m_iSock);
}
int PositiveServer::m_iEpollFd;
string PositiveServer:: m_web_root;
positive_pool_t * PositiveServer::positive_pool;

bool PositiveServer::InitServer()
{
	//监听创建
    m_iEpollFd = epoll_create1(0);
	int keepAlive = 1;//开启keepalive属性
	int keepIdle = 60;//如果60秒内没有任何数据往来，则进行探测
	int keepInterval = 6;//探测时发包的时间间隔为6秒
	int keepCount = 3;//探测尝试的次数，如果第一次探测包就收到相应，以后的2次就不再发
    /* Non-blocking I/O.  */
    if(fcntl(m_iEpollFd, F_SETFL, O_NONBLOCK) == -1)
    {
        perror("fcntl");
        exit(-1);
    }

    m_iSock = socket(AF_INET, SOCK_STREAM, 0);
    if(0 > m_iSock)
    {
        perror("socket");
        exit(1);
    }


    sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(m_http_port);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int ireuseadd_on = 1;//支持端口复用
    //
    setsockopt(m_iSock, SOL_SOCKET, SO_REUSEADDR, &ireuseadd_on, sizeof(ireuseadd_on));
	//keepalive
	setsockopt(m_iSock,SOL_SOCKET, SO_KEEPALIVE, (void*)&keepAlive, sizeof(int));
	setsockopt(m_iSock,SOL_TCP,TCP_KEEPIDLE,(void*)&keepIdle, sizeof(int));
	setsockopt(m_iSock,SOL_TCP, TCP_KEEPINTVL,(void*)&keepInterval,sizeof(int));
	setsockopt(m_iSock,SOL_TCP, TCP_KEEPCNT,(void*)&keepCount,sizeof(int));
	
    if(-1 == bind(m_iSock, (sockaddr*)&listen_addr,sizeof(listen_addr)))
    {
        perror("bind");
        exit(-1);
    }

    if(-1 == listen(m_iSock, _MAX_SOCKFD_COUNT))
    {
        perror("listen");
        exit(-1);
    }


    //监听线程，此线程负责接收客户端连接，加入到epoll中
    if(-1 == pthread_create(&m_ListenThreadId, 0, ListenThread, this))
    {
        perror("pthread_create");
        exit(-1);
    }
    return true;
}
//监听线程
void *PositiveServer::ListenThread(void* lpVoid)
{
    PositiveServer *pTerminalServer = (PositiveServer*)lpVoid;
    sockaddr_in remote_addr;
    int len = sizeof(remote_addr);

    while(true)//监听连接
    {
        int client_socket = accept(pTerminalServer->m_iSock,(sockaddr*)&remote_addr,(socklen_t*)&len);
        if(client_socket < 0)
        {
            //perror("accept");
            continue;
        }
        else
        {
            struct epoll_event ev;
            //起始设置只监听读
            ev.events = POSITIVE_EPOLLIN;
            ev.data.fd = client_socket;
            epoll_ctl(pTerminalServer->m_iEpollFd, EPOLL_CTL_ADD, client_socket, &ev);
        }
    }
}

//处理程序
void PositiveServer::Run()
{
	positiveHttp http;
	int client_socket = 0;
	int nfds = 0;
	//初始化线程池
	http.pool_init(THREAD_NUM);
	for(;;)
	{
		nfds = epoll_wait(m_iEpollFd, events, _MAX_SOCKFD_COUNT, -1);
		for(int i = 0; i < nfds; ++i)
		{
			client_socket = events[i].data.fd;
			if(events[i].events & EPOLLIN)//对读事件进行处理
			{
				http.recvHttpRequest(client_socket, m_iEpollFd);
			}
			//对写事件进行处理
			else if(events[i].events &EPOLLOUT)
			{
				http.sendHttpResponse(client_socket, m_iEpollFd);
			}
			else
			{
				cout << "EPOLL ERROR"<<endl;
				epoll_ctl(m_iEpollFd, EPOLL_CTL_DEL, client_socket, &events[i]);
			}
		}
	}
}
#if 0
void getfiles(const &string path){
	if( path.size() == 0 ){     // 列出根目录

            vector<string> roots;
            Poco::Path::listRoots(roots);

            vector<string>::iterator it;
            WGTP::ResLsDir::ListItem* item;
            for( it = roots.begin(); it != roots.end(); it++ ){
                if( BDUtils::isDriveValid(*it) ){
                    //poco_check_ptr(item = msg.add_list_item());
                    item->set_type(WGTP::FILE_TYPE_DIR);
                    item->set_path(*it);
                }
            }

        }else{                      // 列出目录下的文件和子目录

            // 判断是否目录
            Poco::File file(path);
            if( ! file.isDirectory() ){
                msg.set_result(WGTP::RESRESULT_FALSE);
                msg.set_description(STRING_FALSE);
                return;
            }
         
            Poco::DirectoryIterator it(path);
            Poco::DirectoryIterator end;
            WGTP::ResLsDir::ListItem* item;

            for(; it != end; it++ ){
                try{
                    WGTP::FileType type = WGTP::FILE_TYPE_FILE;
                    if( it->isDirectory() ){
                        type = WGTP::FILE_TYPE_DIR;
                    }
                    poco_check_ptr(item = msg.add_list_item());
                    item->set_type(type);
                    item->set_path(it->path());
                }
                catch(Poco::Exception& exc){
                    wgm_notice(format("List Directory Item '%s' Catch %s: %s", it->path(), std::string(exc.className()), exc.message()));
                }
                catch(std::exception& exc){
                    wgm_notice(format("List Directory Item '%s' Catch Exception %s", it->path(), exc.what()));
                }
            }
        }
}
#endif
void PositiveServer:: scan_dir(string basePath, vector<string>&files,bool searchSubDir)   // 定义目录扫描函数  
{  
   DIR *dir;
   struct dirent *ptr;
   if((dir = opendir(basePath.c_str())) == NULL)
	   return;
   //获取文件列表
   while((ptr = readdir(dir)) != NULL){
	   if(strcmp(ptr->d_name,".") == 0||strcmp(ptr->d_name,"..") == 0){
		   continue;
	   }
	   string curFilePath;
	   curFilePath = basePath + "/" + ptr->d_name;
	   if(ptr->d_type == 4 && searchSubDir){
		   scan_dir(curFilePath,files,searchSubDir);
	   }
	   if(ptr->d_type == 8){
	      files.push_back(curFilePath);
	      //cout<<curFilePath<<endl;
	   }
   }
   closedir(dir);
}  
//把files加入到内存池
void PositiveServer::initFiles()
{
	DIR *dp;
	struct dirent *entry;
	struct stat statbuf;
	//扫描目录，并把文件加入进去
	scan_dir(m_web_root,m_webfiles,true);
	FILE *fp;
	positive_pool_t* temp;
	time_t now;
	positive_pool = (positive_pool_t*)malloc(sizeof(positive_pool_t));
	positive_pool->start = positive_pool;
	positive_pool->accessCount = 0;
	now = time(NULL);
	positive_pool->startTime  = now;
	positive_pool->accessTime = now;
	
	if(positive_pool == NULL)
	{
		perror("malloc positive_pool");
		exit(-1);
	}
	temp =  positive_pool;
	unsigned long filesize = 0;
	for(int i = 0; i < m_webfiles.size(); ++i){
		strcpy(temp->name,m_webfiles.at(i).c_str());
		
		if(lstat(temp->name, &statbuf) < 0){  
           filesize = 0;  
        }else{  
           filesize = statbuf.st_size;  
        }  
		printf("file name : %s\n",temp->name);
		cout<<"file size: "<<filesize<<endl;
		fp = fopen(temp->name,"r");
		if(NULL == fp)
		{
			perror("fopen");
		}
		temp->buffer = (char*)malloc(filesize*sizeof(char));
		fread(temp->buffer,sizeof(char),filesize,fp);
		fclose(fp);
		temp->startTime  = now;
		temp->accessTime = now;
		temp->length = filesize;
		temp->next = (positive_pool_t*)malloc(sizeof(positive_pool_t));
		if(temp->next == NULL)
		{
			perror("malloc temp");
			exit(-1);
		}
		positive_pool->end = temp;
		temp = temp->next;
	}
	positive_pool->end->next = NULL;
	if(temp != positive_pool){
		free(temp);//删除临时节点
		temp = NULL;
	}
	
	//文件管理线程
	#if POOL_SORT
	if(-1 == pthread_create(&m_FileHandlerThreadId,0,fileHandler,NULL))
	{
		perror("pthread_create");
		exit(-1);
	}
	
	//内存池管理
	if(-1 == pthread_create(&m_PoolHandlerThreadId,0,poolHandler,NULL))
	{
		perror("pthread_create");
		exit(-1);
	}
	#endif
}

//检查目录下的文件是否有所改动
void* PositiveServer::fileHandler(void *lpVoid)
{
	positive_pool_t* temp;
	DIR *dp;
	struct dirent *entry;
	struct stat statbuf;
	int flag = 0;
	FILE *fp;
	while(1)
	{
	    //scan_dir("www",m_webfiles,true);
		if((dp = opendir("www")) == NULL)
		{
			perror("opendir html");
			exit(-1);
		}
		while((entry = readdir(dp)) != NULL)
		{
			if(lstat(entry->d_name,&statbuf) == -1)
			{
				perror("lstat");
				exit(-1);
			}
			
			if(S_ISREG(statbuf.st_mode))
			{
				temp =  positive_pool->start;
				flag = 0;

				while(temp != NULL)
				{
					if(strcmp(entry->d_name, temp->name) == 0)
					{
						if(statbuf.st_size != temp->length)//文件已经修改，需要重新导入内存
						{
							//cout<<entry->d_name<<endl;
							flag = 2;
							if(statbuf.st_size <= MAX_FILE_SIZE_IN_POOL)
							{
								fp = fopen(temp->name,"r");
								if(NULL == fp)
								{
									perror("fopen");
									break;
								}
								temp->length = statbuf.st_size;
								//cout<<"free temp buffer"<<endl;
								free(temp->buffer);
								temp->buffer = NULL;
								temp->buffer = (char*)malloc(statbuf.st_size*sizeof(char));
								memset(temp->buffer,0,statbuf.st_size*sizeof(char));
								fread(temp->buffer,sizeof(char),statbuf.st_size,fp);
								fclose(fp);
							}
							//cout<<temp->buffer<<endl;
							break;
						}
						else
						{
							flag = 1;//文件不需要修改
							break;
						}
					}
					temp = temp->next;
				}
				if(0 == flag)//添加新的文件进入内存池
				{
					//cout<<"add new"<<endl;
					temp = positive_pool->end;
					temp->next = (positive_pool_t*)malloc(sizeof(positive_pool_t));
					temp = temp->next;
					if(temp == NULL)
					{
						perror("malloc");
						exit(-1);
					}
					strcpy(temp->name,entry->d_name);
					//cout<<temp->name<<endl;
					//不能超过最大的SIZE
					if(statbuf.st_size <= MAX_FILE_SIZE_IN_POOL)
					{
						fp = fopen(temp->name,"r");
						if(NULL == fp)
						{
							perror("fopen");
						}
						temp->buffer = (char*)malloc(statbuf.st_size*sizeof(char));
						fread(temp->buffer,sizeof(char),statbuf.st_size,fp);
						fclose(fp);
					}
					temp->length = statbuf.st_size;
					temp->accessCount = 0;
					temp->startTime  = time(NULL);
					temp->accessTime = temp->startTime;
					positive_pool->end = temp;//调整end节点
					temp->next = NULL;
				}
			}
		}
		//每10秒检查一次
		sleep(10);
	}
}
positive_pool_t* PositiveServer:: sortLink(positive_pool_t *head)
{
    positive_pool_t *temp = head;
	int flag = 0;
    
	//检查是否要排序
	while(temp->next != NULL)
	{
		if(temp->accessCount <  temp->next->accessCount)
		{
			flag = 1;
			break;
		}
		temp = temp->next;
	}
	if(flag == 0)
	{
		return head;
	}
	
	//真正的排序
	positive_pool_t *pfirst;      /* 排列后有序链的表头指针 */  
    positive_pool_t *ptail;       /* 排列后有序链的表尾指针 */  
    positive_pool_t *pmaxBefore;  /* 保留键值更小的节点的前驱节点的指针 */  
    positive_pool_t *pmax;        /* 存储最小节点   */  
    positive_pool_t *p;           /* 当前比较的节点 */  
	pfirst = NULL;  
    while (head != NULL)         
    {  
		/*在链表中找键值最小的节点。*/  
		for(p=pmax=pmaxBefore = head;p->next != NULL;p = p->next)
		{
			if(pmax->accessCount < p->next->accessCount)
			{
				pmax = p->next;
				pmaxBefore = p;
			}
		}
		if(pfirst == NULL)
		{
			pfirst = pmax;
			ptail  = pmax;
		}
		else{
			ptail->next = pmax;
			ptail = ptail->next;
		}
		
		if(pmax == head)
		{
			head = head->next;//头指针前移
		}
		else{
			pmaxBefore->next = pmax->next;//从原链表中移除pmax
		}
    }  

    if (pfirst != NULL)     /*循环结束得到有序链表first  */  
    {  
        ptail->next = NULL; /*单向链表的最后一个节点的next应该指向NULL */   
    } 
	else{//如果pfirst = NULL说明不需要排序
		return head;
	}
	return 	pfirst;
}
void printLink(positive_pool_t *head)
{
	cout<<"\n+---------------------------+"<<endl;
	while(head != NULL)
	{
		cout<<head->name<<":"<<head->accessCount<<":"<<head->fileState<<endl;
		head = head->next;
	}
	cout<<"+---------------------------+"<<endl;
}
void setState(positive_pool_t *head, int state)
{
	time_t now;
	now = time(NULL);
	
	while(head != NULL)
	{
		head->fileState = state;
		if((now - head->accessTime)>10*60);//超过10分钟没有被访问，那么访问次数减半，相应的在内存池的位置也会靠后
			head->accessCount = head->accessCount/2;
		head = head->next;
	}
}
void* PositiveServer::poolHandler(void *lpVoid)
{
	//排序
	for(;;)
	{
		setState(positive_pool,1);
		positive_pool = sortLink(positive_pool);
		setState(positive_pool,0);
		positive_pool->start = positive_pool;
		//printLink(positive_pool->start);
		sleep(60);
	}
}
//成为守护进程
void PositiveServer::initDaemon()
{
	int pid;
	pid = fork();
	if(pid < 0)
		exit(1);
	else if(pid > 0)
		exit(0);
	setsid();
	cout<<"Positive is running..."<<endl;
}

//加载配置
void PositiveServer::loadConfig(){
	 dictionary  *   ini;
	 const char* ini_name = "config.ini";
	 ini = iniparser_load(ini_name);
	 
	 if (ini == NULL) {        
	 	fprintf(stderr, "cannot parse file: %s\n", ini_name);        
		return;    
	 }
	 //监听端口
	 m_http_port = iniparser_getint(ini, "global:port", NULL);
	 cout<<m_http_port<<endl;
	 //网站路径
	 m_web_root = iniparser_getstring(ini,"global:web_root",NULL);
	 cout<<m_web_root<<endl;
	 m_max_socket_num = iniparser_getint(ini, "global:max_socket_num", NULL);
	 cout<<m_max_socket_num<<endl;
	 m_max_file_size_in_pool = iniparser_getint(ini, "global:max_file_size_in_pool", NULL);
	 cout<<m_max_file_size_in_pool<<endl;
	 m_http_buffer_size = iniparser_getint(ini, "global:http_buffer_size", NULL);
	 cout<<m_http_buffer_size<<endl;
	 m_http_head_size = iniparser_getint(ini, "global:http_head_size", NULL);
	 cout<<m_http_head_size<<endl;
	 m_thread_num = iniparser_getint(ini, "global:thread_num", NULL);
	 cout<<m_thread_num<<endl;
	 iniparser_freedict(ini);
}
int main(int argc, char** argv)
{
	
    PositiveServer server;
	server.loadConfig();
	//server.initDaemon();
	server.initFiles();
    server.InitServer();
    server.Run();
	
    return 0;
}