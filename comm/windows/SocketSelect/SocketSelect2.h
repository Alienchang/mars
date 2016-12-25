/*  Copyright (c) 2013-2015 Tencent. All rights reserved.  */

#ifndef _SOCKSTSELECT_
#define _SOCKSTSELECT_

#include <map>
#include "thread/lock.h"
#include "socket/unix_socket.h"

class SocketSelect;
class SocketSelectBreaker {
    friend SocketSelect;
  public:
    SocketSelectBreaker();
    ~SocketSelectBreaker();

    bool IsCreateSuc() const;
    bool ReCreate();

    bool IsBreak() const;

    bool Break();
    bool Clear();
    void Close();

    WSAEVENT BreakerFD() const;

  private:
    SocketSelectBreaker(const SocketSelectBreaker&);
    SocketSelectBreaker& operator=(const SocketSelectBreaker&);

  private:
    Mutex m_mutex;
    WSAEVENT m_event;
    bool m_create_success;
    bool m_broken;
    int m_exception;
};

class SocketSelect {
  public:
    SocketSelect(SocketSelectBreaker& _breaker, bool _autoclear = false);
    ~SocketSelect();

    void PreSelect();
    void Read_FD_SET(SOCKET _socket);
    void Write_FD_SET(SOCKET _socket);
    void Exception_FD_SET(SOCKET _socket);
    int Select();
    int Select(int _msec);
    int Select(int _sec, int _usec);

    int Errno() const;

    int Read_FD_ISSET(SOCKET _socket) const;
    int Write_FD_ISSET(SOCKET _socket) const;
    int Exception_FD_ISSET(SOCKET _socket) const;

    bool IsBreak() const;
    bool IsException() const;

    SocketSelectBreaker& Breaker();

  private:
    SocketSelect(const SocketSelect&);
    SocketSelect& operator=(const SocketSelect&);

  private:
    const bool autoclear_;
    SocketSelectBreaker& breaker_;
    bool m_broken;

    std::map<SOCKET, int> m_filter_map;
    int errno_;

    fd_set writefd_;
    fd_set readfd_;
    fd_set exceptionfd_;
};

#endif
