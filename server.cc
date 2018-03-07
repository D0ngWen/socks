//
//  server.cc
//
//  Created by 吴怡生 on 1/03/2018.
//  Copyright © 2018 yeshen.org. All rights reserved.
//

#include "server.h"

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <map>

#define SERVER_PORT 1080
#define MAX_CONNECTION 10
#define MAX_DATA_SIZE 1000
#define MAX_EVENT 32
#define AUTHON_TYPE 0

#define ATYP_IPV4 1
#define ATYP_DOMAIN_NAME 3
#define ATYP_IPV6 4

#define REP_succeeded '\x00'
#define REP_server_failure '\x01'
#define REP_not_allowed '\x02'
#define REP_Network_unreachable '\x03'
#define REP_Host_unreachable '\x04'
#define REP_refused '\x05'
#define REP_TTL_expired '\x06'
#define REP_Command_not_supported '\x07'
#define REP_Address_not_supported '\x08'

#define CMD_CONNECT '\x01'
#define CMD_BIND '\x02'
#define CMD_UDP_ASSOCIATE '\x03'

#define STATUS_INIT 0
#define STATUS_REQUEST 1
#define STATUS_READY 2
#define STATUS_CLOSE 3

using namespace std;

Section::Section(int inner_fd){
    this->status = STATUS_INIT;
    this->inner = inner_fd;
    this->outter = -1;
};

bool Section::ready(){
    return this->status == STATUS_READY;
};

bool Section::handshak(){
    int fd = this->inner;
    switch(this->status){
        case STATUS_INIT:{
            char buf[3];
            int count = read(fd,buf,3);
            char VER = buf[0];
            char NMETHODS = buf[1];
            char METHODS = buf[2];
            if(VER == 0x05){
                if(METHODS == '\x00'){
                    if(send(fd,"\x05\x00",2,0) != -1){
                        this->status = STATUS_REQUEST;
                        return true;
                    }
                }else if(METHODS == '\x02'){
                    //TODO
                    if(send(fd,"\x05\x00",2,0) != -1){
                        this->status = STATUS_REQUEST;
                        return true;
                    }
                }
            }
            send(fd,"\x05\xFF\n",3,0);
            return false;
        }
        case STATUS_REQUEST:{
            char buf[512];
            int count = read(fd,buf,512);
            if(count > 0){
                char VER = buf[0];
                char CMD = buf[1];
                char RSV = buf[2];
                char ATYP = buf[3];
                this->cmd = CMD;
                if (ATYP == ATYP_IPV4){
                    in_addr thost;
                    char *y = (char *)&thost;
                    y[0]=buf[4];
                    y[1]=buf[5];
                    y[2]=buf[6];
                    y[3]=buf[7];
                    this->host = inet_ntoa(*(struct in_addr *)&thost);
                    int intport =(int)(buf[8]) << 8 | (int)(buf[9]);
                    char buffer [33];
                    sprintf(buffer, "%d", intport);
                    this->port=buffer;
                }else if (ATYP == ATYP_DOMAIN_NAME){
                    char host_length = buf[4];
                    char temp[host_length];
                    strncpy(temp,buf+5,host_length);
                    this->host = temp;
                    int intport = (int)(buf[5+host_length]) << 8 | (int)(buf[5+host_length+1]);
                    char buffer [33];
                    sprintf(buffer, "%d", intport);
                    this->port=buffer;
                }else if (ATYP == ATYP_IPV6){
                    return false;
                }
            }
            char rep;
            if((rep=this->connet()) != REP_succeeded){
                char * reply =(char *)"\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00";
                reply[1] = rep;
                if(send(fd,reply,10,0) != -1){
                    this->status = STATUS_READY;
                    return true;
                }
            }else{
                this->destory();
            }
            break;
        }
        case STATUS_READY:
        case STATUS_CLOSE:{
            //do nothing when ready/close
            break;
        }
        default:{
            perror("unknow status");
            return false;
        }

    }
    return true;
};

char Section::connet(){
    if(this->cmd == CMD_CONNECT){
        int socket_fd = -1;
        in_addr_t server_ip = inet_addr(this->host);
        in_port_t server_port = atoi(this->port);
        struct sockaddr_in target;
        if((socket_fd = socket(AF_INET,SOCK_STREAM,0)) == -1){
            perror("fail to create socket");
            return REP_Network_unreachable;
        }
        memset(&target,0,sizeof(target));
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = server_ip;
        target.sin_port = htons(server_port);
        if(connect(socket_fd,(struct sockaddr *)&target,sizeof(target))){
            this->outter = socket_fd;
            return REP_succeeded;
        }else{
            return REP_Network_unreachable;
        }
    }else if(this->cmd == CMD_UDP_ASSOCIATE){
        int socket_fd = -1;
        in_addr_t server_ip = inet_addr(this->host);
        in_port_t server_port = atoi(this->port);
        struct sockaddr_in target;
        if((socket_fd = socket(AF_INET,SOCK_DGRAM,0)) == -1){
            perror("fail to create socket");
            return REP_Network_unreachable;
        }
        memset(&target,0,sizeof(target));
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = server_ip;
        target.sin_port = htons(server_port);
        if(connect(socket_fd,(struct sockaddr*)&target,sizeof(target))){
            this->outter = socket_fd;
            return REP_succeeded;
        }else{
            return REP_Network_unreachable;
        }
        printf("TODO UDP_ASSOCIATE");
        return REP_succeeded;
    }
};

bool Section::forward(int from){
    if(this->cmd == CMD_CONNECT){
        int to = from == this->inner ? this->outter: this-> inner;
        while(true){
            char buf[512];
            int count = read(from,buf,512);
            if(count == -1){
                if(errno == EAGAIN){
                    continue;
                }
            }else if(count ==0){
                return false;
            }else if(count > 0){
                if(send(to,buf,count,0) != -1){
                    perror("forward send error");
                    return false;
                }else{
                    return true;
                }
            }
        }
    }else if(this->cmd == CMD_UDP_ASSOCIATE){
        printf("TODO UDP_ASSOCIATE");
    }
};

void Section::destory(){
    close(this->inner);
    close(this->outter);
}
/*---section end---*/

SectionPool::SectionPool(){};

void SectionPool::put(Section section){
    this->data.insert(std::make_pair(section.inner, section));
};

void SectionPool::remove(Section section){
    this->data.erase(section.inner);
};

void SectionPool::remove(int fd){
    this->data.erase(fd);
};

map<int, Section>::iterator SectionPool::find(int fd){
    return this->data.find(fd);
};
/*----section pool end------*/

Server::Server(){};

bool Server::set_nonblocking(int fd){
    int sock_flags = fcntl(fd,F_GETFL,0);
    sock_flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, sock_flags) != -1;
};

int Server::watch_port(int port){
    int sock_fd = -1;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    if((sock_fd = socket(AF_INET,SOCK_STREAM,0))==-1){
        perror("fail to create socket");
        exit(1);
    }
    long flag = 1;
    setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR,(char *)&flag,sizeof(flag));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(local_addr.sin_zero),8);

    if(bind(sock_fd,(struct sockaddr *)&local_addr,sizeof(struct sockaddr)) == -1){
        perror("fail to bind socket");
        exit(1);
    }
    if(!this->set_nonblocking(sock_fd)){
        perror("fail to set nonblock");
        exit(1);
    }
    if(listen(sock_fd,MAX_CONNECTION) == -1){
        perror("fail to listen");
        exit(1);
    }
    return sock_fd;
};

bool Server::add_into_epoll(int sock_fd){
    struct epoll_event event = (struct epoll_event)*this->event;
    event.data.fd = sock_fd;
    event.events = EPOLLIN | EPOLLET;
    return epoll_ctl(this->epoll_fd,EPOLL_CTL_ADD,sock_fd,&event) != -1;
};

bool Server::accept_connect(int sock_fd){
    struct sockaddr in_addr;
    socklen_t in_len = sizeof(in_addr);
    int in_fd;
    if((in_fd = accept(sock_fd,&in_addr,&in_len)) == -1){
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            return false;
        }else{
            perror("accept failed");
            return false;
        }
    }

    std::string hbuf(NI_MAXHOST,'\0');
    std::string sbuf(NI_MAXSERV,'\0');
    if(getnameinfo(&in_addr,in_len,
        const_cast<char*>(hbuf.data()),hbuf.size(),
        const_cast<char*>(sbuf.data()),sbuf.size(),
        NI_NUMERICHOST | NI_NUMERICSERV) == 0 ){
            
        std::cout << "Accepted:" << in_fd << "(host=" << hbuf << ", port=" << sbuf << ")" << endl;
    }

    if(!this->set_nonblocking(in_fd)){
        std::cout << "nonblocking" << "(host=" << hbuf << endl;
        return false;
    }
    if(!this->add_into_epoll(in_fd)){
        printf("fail to add epoll\n");
        return false;
    }
    Section section(in_fd);
    this->pool.put(section);
    return true;
};

bool Server::close_connect(int fd){
    //TODO
};

void Server::handle(int from){
    map<int, Section>::iterator iter = this->pool.find(from); 
    if(iter != this->pool.data.end()){
        Section section = iter->second;
        if(section.ready()){
            if(!section.forward(from)){
                section.destory();
                this->close_connect(from);
            }
        }else{
            if(section.handshak()){
                if(section.ready()){
                    if(!this->add_into_epoll(section.outter)){
                        printf("fail to add epoll\n");
                    }
                }
            }else{
                // handshak fail
                section.destory();
                this->close_connect(from);
            }
        }
    }else{
        perror("noknow connect");
        this->close_connect(from);
    }
};

void Server::forever(){
    cout << "start" << endl;

    int epoll_fd;
    struct epoll_event event,events[MAX_EVENT];
    int sock_fd = this->watch_port(SERVER_PORT);
    if((epoll_fd = epoll_create1(0))==-1){
        perror("fail to create epoll");
        exit(1);
    }
    this->epoll_fd = epoll_fd;
    this->event = &event;

    if(!this->add_into_epoll(sock_fd)){
        perror("fail to add into epoll");
        exit(1);
    }

    while(true){
        int n = epoll_wait(epoll_fd,events,MAX_EVENT,-1);
        for(int i =0;i<n; ++i){
            if(events[i].events & EPOLLERR || 
                events[i].events & EPOLLHUP ||
                !(events[i].events & EPOLLIN) ){
                    perror("error in epoll event");
                    close(events[i].data.fd);
            }else if(sock_fd == events[i].data.fd){
                while(this->accept_connect(sock_fd)){ /*keep empty*/}
            }else{
                this->handle(events[i].data.fd);
            }
        }
    }
    close(sock_fd);
};