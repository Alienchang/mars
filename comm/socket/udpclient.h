/*  Copyright (c) 2013-2015 Tencent. All rights reserved.  */
/*
 * UdpClient.h
 *
 *  Created on: 2014-9-5
 *      Author: zhouzhijie
 */

#ifndef UDPCLIENT_H_
#define UDPCLIENT_H_

#include <string>
#include <list>

#include "comm/socket/unix_socket.h"
#include "comm/socket/socketselect.h"
#include "comm/thread/thread.h"
#include "comm/thread/mutex.h"
#include "comm/autobuffer.h"

#define IPV4_BROADCAST_IP "255.255.255.255"

struct UdpSendData;
class UdpClient;

class IAsyncUdpClientEvent {
  public:
    virtual ~IAsyncUdpClientEvent() {}
    virtual void OnError(UdpClient* _this, int _errno) = 0;
    virtual void OnDataGramRead(UdpClient* _this, void* _buf, size_t _len) = 0;
    virtual void OnDataSent(UdpClient* _this) = 0;
};

class UdpClient {
  public:
    UdpClient(const std::string& _ip, int _port);
    UdpClient(const std::string& _ip, int _port, IAsyncUdpClientEvent* _event);
    ~UdpClient();

    /*
     * return -2 break, -1 error, 0 timeout, else handle size
     */
    int SendBlock(void* _buf, size_t _len);
    int ReadBlock(void* _buf, size_t _len, int _timeOutMs = -1);

    void Break() { breaker_.Break(); }

    bool HasBuuferToSend();
    void SendAsync(void* _buf, size_t _len);

    void SetIpPort(const std::string& _ip, int _port);

  private:
    void __InitSocket(const std::string& _ip, int _port);
    int __DoSelect(bool _bReadSet, bool _bWriteSet, void* _buf, size_t _len, int& _errno, int _timeoutMs);
    void __RunLoop();

  private:
    SOCKET fd_socket_;
    struct sockaddr_in addr_;
    IAsyncUdpClientEvent* event_;

    SocketSelectBreaker breaker_;
    SocketSelect selector_;
    Thread* thread_;

    std::list<UdpSendData> list_buffer_;
    Mutex mutex_;
};

#endif /* UDPCLIENT_H_ */
