/*  Copyright (c) 2013-2015 Tencent. All rights reserved.  */
/*
 * MessageQueue.cpp
 *
 *  Created on: 2013-4-3
 *      Author: yerungui
 */

#include <map>
#include <list>
#include <string>
#include <algorithm>

#include "boost/bind.hpp"

#include "comm/thread/lock.h"
#include "comm/anr.h"
#include "comm/messagequeue/message_queue.h"
#include "comm/time_utils.h"
#ifdef __APPLE__
#include "comm/debugger/debugger_utils.h"
#endif
#undef min

namespace MessageQueue {

static unsigned int __MakeSeq() {
    static unsigned int s_seq = 0;

    return ++s_seq;
}

struct MessageWrapper {
    MessageWrapper(const MessageHandler_t& _handlerid, const Message& _message, const MessageTiming& _timing, unsigned int _seq)
        : message(_message), timing(_timing) {
        postid.reg = _handlerid;
        postid.seq = _seq;
        periodstatus = kImmediately;
        record_time = 0;

        if (kImmediately != _timing.type) {
            periodstatus = kAfter;
            record_time = ::gettickcount();
        }
    }

    ~MessageWrapper() {
        if (wait_end_cond)
            wait_end_cond->notifyAll();
    }

    MessagePost_t postid;
    Message message;

    MessageTiming timing;
    TMessageTiming periodstatus;
    uint64_t record_time;
    boost::shared_ptr<Condition> wait_end_cond;
};

struct HandlerWrapper {
    HandlerWrapper(const MessageHandler& _handler, bool _recvbroadcast, const MessageQueue_t& _messagequeueid, unsigned int _seq)
        : handler(_handler), recvbroadcast(_recvbroadcast) {
        reg.seq = _seq;
        reg.queue = _messagequeueid;
    }

    MessageHandler_t reg;
    MessageHandler handler;
    bool recvbroadcast;
};

struct RunLoopInfo {
    RunLoopInfo():runing_message(NULL) { runing_cond = boost::make_shared<Condition>();}
    
    boost::shared_ptr<Condition> runing_cond;
    MessagePost_t runing_message_id;
    Message* runing_message;
    std::list <MessageHandler_t> runing_handler;
};
    
class Cond : public RunloopCond {
public:
    Cond(){}
    
public:
    const boost::typeindex::type_info& type() const {
        return boost::typeindex::type_id<Cond>().type_info();
    }
    
    virtual void Wait(ScopedLock& _lock, long _millisecond) {
        cond_.wait(_lock, _millisecond);
    }
    virtual void Notify(ScopedLock& _lock) {
        cond_.notifyAll(_lock);
    }
    
private:
    Cond(const Cond&);
    void operator=(const Cond&);
    
private:
    Condition cond_;
};
    
struct MessageQueueContent {
public:
    MessageQueueContent(): breakflag(false), anr_timeout(-1) {}

    MessageHandler_t invoke_reg;
    bool breakflag;
    boost::shared_ptr<RunloopCond> breaker;
    std::list<MessageWrapper*> lst_message;
    std::list<HandlerWrapper*> lst_handler;
    int anr_timeout;
    
    std::list<RunLoopInfo> lst_runloop_info;
};

#define sg_messagequeue_map_mutex messagequeue_map_mutex()
static Mutex& messagequeue_map_mutex() {
    static Mutex* mutex = new Mutex;
    return *mutex;
}
#define sg_messagequeue_map messagequeue_map()
static std::map<MessageQueue_t, MessageQueueContent>& messagequeue_map() {
    static std::map<MessageQueue_t, MessageQueueContent>* mq_map = new std::map<MessageQueue_t, MessageQueueContent>;
    return *mq_map;
}

MessageQueue_t CurrentThreadMessageQueue() {
    ScopedLock lock(sg_messagequeue_map_mutex);
    MessageQueue_t id = (MessageQueue_t)ThreadUtil::currentthreadid();

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) id = KInvalidQueueID;

    return id;
}

MessageQueue_t TID2MessageQueue(thread_tid _tid) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    MessageQueue_t id = (MessageQueue_t)_tid;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id))id = KInvalidQueueID;

    return id;
}
    
thread_tid  MessageQueue2TID(MessageQueue_t _id) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    MessageQueue_t& id = _id;
    
    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return 0;
    
    return (thread_tid)id;
}

void WaitForRuningLockEnd(const MessagePost_t&  _message) {
    if (Handler2Queue(Post2Handler(_message)) == CurrentThreadMessageQueue()) return;

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = Handler2Queue(Post2Handler(_message));

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return;
    MessageQueueContent& content = sg_messagequeue_map[id];
    
    if (content.lst_runloop_info.empty()) return;
    
    auto find_it = std::find_if(content.lst_runloop_info.begin(), content.lst_runloop_info.end(),
                                [&_message](const RunLoopInfo& _v){ return _message == _v.runing_message_id; });
    
    if (find_it == content.lst_runloop_info.end()) return;

    boost::shared_ptr<Condition> runing_cond = find_it->runing_cond;
    runing_cond->wait(lock);
}

void WaitForRuningLockEnd(const MessageQueue_t&  _messagequeueid) {
    if (_messagequeueid == CurrentThreadMessageQueue()) return;

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _messagequeueid;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return;
    MessageQueueContent& content = sg_messagequeue_map[id];

    if (content.lst_runloop_info.empty()) return;
    if (KNullPost == content.lst_runloop_info.front().runing_message_id) return;

    boost::shared_ptr<Condition> runing_cond = content.lst_runloop_info.front().runing_cond;
    runing_cond->wait(lock);
}

void WaitForRuningLockEnd(const MessageHandler_t&  _handler) {
    if (Handler2Queue(_handler) == CurrentThreadMessageQueue()) return;

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = Handler2Queue(_handler);

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) { return; }
    MessageQueueContent& content = sg_messagequeue_map[id];
    if (content.lst_runloop_info.empty()) return;

    for(auto& i : content.lst_runloop_info) {
        for (auto& x : i.runing_handler) {
            if (_handler==x) {
                boost::shared_ptr<Condition> runing_cond = i.runing_cond;
                runing_cond->wait(lock);
                return;
            }
        }
    }
}

void BreakMessageQueueRunloop(const MessageQueue_t&  _messagequeueid) {
    ASSERT(0 != _messagequeueid);

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _messagequeueid;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        ASSERT2(false, "%llu", id);
        return;
    }

    sg_messagequeue_map[id].breakflag = true;
    sg_messagequeue_map[id].breaker->Notify(lock);
}

MessageHandler_t InstallMessageHandler(const MessageHandler& _handler, bool _recvbroadcast, const MessageQueue_t& _messagequeueid) {
    ASSERT(bool(_handler));

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _messagequeueid;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        ASSERT2(false, "%llu", id);
        return KNullHandler;
    }

    HandlerWrapper* handler = new HandlerWrapper(_handler, _recvbroadcast, _messagequeueid, __MakeSeq());
    sg_messagequeue_map[id].lst_handler.push_back(handler);
    return handler->reg;
}

void UnInstallMessageHandler(const MessageHandler_t& _handlerid) {
    ASSERT(0 != _handlerid.queue);
    ASSERT(0 != _handlerid.seq);

    if (0 == _handlerid.queue || 0 == _handlerid.seq) return;

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _handlerid.queue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return;

    MessageQueueContent& content = sg_messagequeue_map[id];

    for (std::list<HandlerWrapper*>::iterator it = content.lst_handler.begin(); it != content.lst_handler.end(); ++it) {
        if (_handlerid == (*it)->reg) {
            delete(*it);
            content.lst_handler.erase(it);
            break;
        }
    }
}

MessagePost_t PostMessage(const MessageHandler_t& _handlerid, const Message& _message, const MessageTiming& _timing) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _handlerid.queue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        ASSERT2(false, "%llu", id);
        return KNullPost;
    }

    MessageQueueContent& content = sg_messagequeue_map[id];

    MessageWrapper* messagewrapper = new MessageWrapper(_handlerid, _message, _timing, __MakeSeq());

    content.lst_message.push_back(messagewrapper);
    content.breaker->Notify(lock);
    return messagewrapper->postid;
}

MessagePost_t SingletonMessage(bool _replace, const MessageHandler_t& _handlerid, const Message& _message, const MessageTiming& _timing) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _handlerid.queue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return KNullPost;

    MessageQueueContent& content = sg_messagequeue_map[id];

    MessagePost_t post_id;

    for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end(); ++it) {
        if ((*it)->postid.reg == _handlerid && (*it)->message == _message) {
            if (_replace) {
                post_id = (*it)->postid;
                delete(*it);
                content.lst_message.erase(it);
                break;
            } else {
                return (*it)->postid;
            }
        }
    }

    MessageWrapper* messagewrapper = new MessageWrapper(_handlerid, _message, _timing, 0 != post_id.seq ? post_id.seq : __MakeSeq());
    content.lst_message.push_back(messagewrapper);
    content.breaker->Notify(lock);
    return messagewrapper->postid;
}

MessagePost_t BroadcastMessage(const MessageQueue_t& _messagequeueid,  const Message& _message, const MessageTiming& _timing) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _messagequeueid;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        ASSERT2(false, "%llu", id);
        return KNullPost;
    }

    MessageQueueContent& content = sg_messagequeue_map[id];

    MessageHandler_t reg;
    reg.queue = _messagequeueid;
    reg.seq = 0;
    MessageWrapper* messagewrapper = new MessageWrapper(reg, _message, _timing, __MakeSeq());

    content.lst_message.push_back(messagewrapper);
    content.breaker->Notify(lock);
    return messagewrapper->postid;
}

static int64_t __ComputerWaitTime(const MessageWrapper& _wrap) {
    int64_t wait_time = 0;

    if (kImmediately == _wrap.timing.type) {
        wait_time = 0;
    } else if (kAfter == _wrap.timing.type) {
        int64_t time_cost = ::gettickspan(_wrap.record_time);
        wait_time =  _wrap.timing.after - time_cost;
    } else if (kPeriod == _wrap.timing.type) {
        int64_t time_cost = ::gettickspan(_wrap.record_time);

        if (kAfter == _wrap.periodstatus) {
            wait_time =  _wrap.timing.after - time_cost;
        } else if (kPeriod == _wrap.periodstatus) {
            wait_time =  _wrap.timing.period - time_cost;
        }
    }

    return 0 < wait_time ? wait_time : 0;
}

MessagePost_t FasterMessage(const MessageHandler_t& _handlerid, const Message& _message, const MessageTiming& _timing) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _handlerid.queue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return KNullPost;

    MessageQueueContent& content = sg_messagequeue_map[id];

    MessageWrapper* messagewrapper = new MessageWrapper(_handlerid, _message, _timing, __MakeSeq());

    for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end(); ++it) {
        if ((*it)->postid.reg == _handlerid && (*it)->message == _message) {
            if (__ComputerWaitTime(**it) < __ComputerWaitTime(*messagewrapper)) {
                delete messagewrapper;
                return (*it)->postid;
            }

            messagewrapper->postid = (*it)->postid;
            delete(*it);
            content.lst_message.erase(it);
            break;
        }
    }

    content.lst_message.push_back(messagewrapper);
    content.breaker->Notify(lock);
    return messagewrapper->postid;
}

bool WaitMessage(const MessagePost_t& _message) {
    bool is_in_mq = Handler2Queue(Post2Handler(_message)) == CurrentThreadMessageQueue();

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = Handler2Queue(Post2Handler(_message));
    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return false;
    MessageQueueContent& content = sg_messagequeue_map[id];

    auto find_it = std::find_if(content.lst_message.begin(), content.lst_message.end(),
                                [&_message](const MessageWrapper * const &_v) {
                                    return _message == _v->postid;
                                });
    
    if (find_it == content.lst_message.end()) {
        auto find_it = std::find_if(content.lst_runloop_info.begin(), content.lst_runloop_info.end(),
                     [&_message](const RunLoopInfo& _v){ return _message == _v.runing_message_id; });
        
        if (find_it != content.lst_runloop_info.end()) {
            if (is_in_mq) return false;
            
            boost::shared_ptr<Condition> runing_cond = find_it->runing_cond;
            runing_cond->wait(lock);
        }
    } else {
        
        if (is_in_mq) {
            lock.unlock();
            RunLoop( [&_message](){
                        MessageQueueContent& content = sg_messagequeue_map[Handler2Queue(Post2Handler(_message))];
                        return content.lst_message.end() == std::find_if(content.lst_message.begin(), content.lst_message.end(),
                                                                [&_message](const MessageWrapper *  const &_v) {
                                                                    return _message == _v->postid;
                                                                });
            }).Run();
            
        } else {
            if (!((*find_it)->wait_end_cond))(*find_it)->wait_end_cond = boost::make_shared<Condition>();

            boost::shared_ptr<Condition> wait_end_cond = (*find_it)->wait_end_cond;
            wait_end_cond->wait(lock);
        }
    }

    return true;
}

bool FoundMessage(const MessagePost_t& _message) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = Handler2Queue(Post2Handler(_message));

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return false;
    MessageQueueContent& content = sg_messagequeue_map[id];
    if (content.lst_runloop_info.empty()) return false;

    auto find_it = std::find_if(content.lst_runloop_info.begin(), content.lst_runloop_info.end(),
                                [&_message](const RunLoopInfo& _v){ return _message == _v.runing_message_id; });
    
    if (find_it != content.lst_runloop_info.end())  { return true; }

    for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end(); ++it) {
        if (_message == (*it)->postid) { return true;}
    }

    return false;
}

bool CancelMessage(const MessagePost_t& _postid) {
    ASSERT(0 != _postid.reg.queue);
    ASSERT(0 != _postid.seq);

    // 0==_postid.reg.seq for BroadcastMessage
    if (0 == _postid.reg.queue || 0 == _postid.seq) return false;

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _postid.reg.queue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        ASSERT2(false, "%llu", id);
        return false;
    }

    MessageQueueContent& content = sg_messagequeue_map[id];

    for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end(); ++it) {
        if (_postid == (*it)->postid) {
            delete(*it);
            content.lst_message.erase(it);
            return true;
        }
    }

    return false;
}

void CancelMessage(const MessageHandler_t& _handlerid) {
    ASSERT(0 != _handlerid.queue);

    // 0==_handlerid.seq for BroadcastMessage
    if (0 == _handlerid.queue) return;

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _handlerid.queue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        //        ASSERT2(false, "%lu", id);
        return;
    }

    MessageQueueContent& content = sg_messagequeue_map[id];

    for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end();) {
        if (_handlerid == (*it)->postid.reg) {
            delete(*it);
            it = content.lst_message.erase(it);
        } else {
            ++it;
        }
    }
}

void CancelMessage(const MessageHandler_t& _handlerid, const MessageTitle_t& _title) {
    ASSERT(0 != _handlerid.queue);

    // 0==_handlerid.seq for BroadcastMessage
    if (0 == _handlerid.queue) return;

    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _handlerid.queue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        ASSERT2(false, "%llu", id);
        return;
    }

    MessageQueueContent& content = sg_messagequeue_map[id];

    for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end();) {
        if (_handlerid == (*it)->postid.reg && _title == (*it)->message.title) {
            delete(*it);
            it = content.lst_message.erase(it);
        } else {
            ++it;
        }
    }
}
    
const Message& RuningMessage() {
    MessageQueue_t id = (MessageQueue_t)ThreadUtil::currentthreadid();
    ScopedLock lock(sg_messagequeue_map_mutex);
    
    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        return KNullMessage;
    }
    
    MessageQueueContent& content = sg_messagequeue_map[id];
    return *(content.lst_runloop_info.back().runing_message);
}
    
MessagePost_t RuningMessageID() {
    MessageQueue_t id = (MessageQueue_t)ThreadUtil::currentthreadid();
    return RuningMessageID(id);
}

MessagePost_t RuningMessageID(const MessageQueue_t& _id) {
    ScopedLock lock(sg_messagequeue_map_mutex);

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(_id)) {
        return KNullPost;
    }

    MessageQueueContent& content = sg_messagequeue_map[_id];
    return content.lst_runloop_info.back().runing_message_id;
}

static void __AsyncInvokeHandler(const MessagePost_t& _id, Message& _message) {
    (*boost::any_cast<boost::shared_ptr<AsyncInvokeFunction> >(_message.body1))();
}

MessageHandler_t InstallAsyncHandler(const MessageQueue_t& id) {
    ASSERT(0 != id);
    return InstallMessageHandler(__AsyncInvokeHandler, false, id);
}
    

static MessageQueue_t __CreateMessageQueueInfo(boost::shared_ptr<RunloopCond>& _breaker, thread_tid _tid, int _anr_timeout = 10*60*1000) {
    ScopedLock lock(sg_messagequeue_map_mutex);

    MessageQueue_t id = (MessageQueue_t)_tid;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) {
        MessageQueueContent& content = sg_messagequeue_map[id];
        HandlerWrapper* handler = new HandlerWrapper(&__AsyncInvokeHandler, false, id, __MakeSeq());
        content.lst_handler.push_back(handler);
        content.invoke_reg = handler->reg;
        content.anr_timeout = _anr_timeout;
        if (_breaker)
            content.breaker = _breaker;
        else
            content.breaker = boost::make_shared<Cond>();
        content.breakflag = false;
    }

    return id;
}
    
static void __ReleaseMessageQueueInfo() {

    MessageQueue_t id = (MessageQueue_t)ThreadUtil::currentthreadid();

    if (sg_messagequeue_map.end() != sg_messagequeue_map.find(id)) {
        MessageQueueContent& content = sg_messagequeue_map[id];

        for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end(); ++it) {
            delete(*it);
        }

        for (std::list<HandlerWrapper*>::iterator it = content.lst_handler.begin(); it != content.lst_handler.end(); ++it) {
            delete(*it);
        }

        sg_messagequeue_map.erase(id);
    }
}

void RunLoop::Run() {
    MessageQueue_t id = CurrentThreadMessageQueue();
    ASSERT(0 != id);
    {
        ScopedLock lock(sg_messagequeue_map_mutex);
        sg_messagequeue_map[id].lst_runloop_info.push_back(RunLoopInfo());
    }

    while (true) {
        ScopedLock lock(sg_messagequeue_map_mutex);
        MessageQueueContent& content = sg_messagequeue_map[id];
        content.lst_runloop_info.back().runing_message_id = KNullPost;
        content.lst_runloop_info.back().runing_message = NULL;
        content.lst_runloop_info.back().runing_handler.clear();
        content.lst_runloop_info.back().runing_cond->notifyAll(lock);

        if ((content.breakflag || (breaker_func_ && breaker_func_()))) {
            content.lst_runloop_info.pop_back();
            if (content.lst_runloop_info.empty())
                __ReleaseMessageQueueInfo();
            break;
        }

        int64_t wait_time = 10 * 60 * 1000;
        MessageWrapper* messagewrapper = NULL;
        bool delmessage = true;

        for (std::list<MessageWrapper*>::iterator it = content.lst_message.begin(); it != content.lst_message.end(); ++it) {
            if (kImmediately == (*it)->timing.type) {
                messagewrapper = *it;
                content.lst_message.erase(it);
                break;
            } else if (kAfter == (*it)->timing.type) {
                int64_t time_cost = ::gettickspan((*it)->record_time);

                if ((*it)->timing.after <= time_cost) {
                    messagewrapper = *it;
                    content.lst_message.erase(it);
                    break;
                } else {
                    wait_time = std::min(wait_time, (*it)->timing.after - time_cost);
                }
            } else if (kPeriod == (*it)->timing.type) {
                if (kAfter == (*it)->periodstatus) {
                    int64_t time_cost = ::gettickspan((*it)->record_time);

                    if ((*it)->timing.after <= time_cost) {
                        messagewrapper = *it;
                        (*it)->record_time = ::gettickcount();
                        (*it)->periodstatus = kPeriod;
                        delmessage = false;
                        break;
                    } else {
                        wait_time = std::min(wait_time, (*it)->timing.after - time_cost);
                    }
                } else if (kPeriod == (*it)->periodstatus) {
                    int64_t time_cost = ::gettickspan((*it)->record_time);

                    if ((*it)->timing.period <= time_cost) {
                        messagewrapper = *it;
                        (*it)->record_time = ::gettickcount();
                        delmessage = false;
                        break;
                    } else {
                        wait_time = std::min(wait_time, (*it)->timing.period - time_cost);
                    }
                } else {
                    ASSERT(false);
                }
            } else {
                ASSERT(false);
            }
        }

        if (NULL == messagewrapper) {
            content.breaker->Wait(lock, (long)wait_time);
            continue;
        }

        std::list<HandlerWrapper> fit_handler;

        for (std::list<HandlerWrapper*>::iterator it = content.lst_handler.begin(); it != content.lst_handler.end(); ++it) {
            if (messagewrapper->postid.reg == (*it)->reg || ((*it)->recvbroadcast && messagewrapper->postid.reg.isbroadcast())) {
                fit_handler.push_back(**it);
                content.lst_runloop_info.back().runing_handler.push_back((*it)->reg);
            }
        }

        content.lst_runloop_info.back().runing_message_id = messagewrapper->postid;
        content.lst_runloop_info.back().runing_message = &messagewrapper->message;
        int anr_timeout = content.anr_timeout;
        lock.unlock();

        for (std::list<HandlerWrapper>::iterator it = fit_handler.begin(); it != fit_handler.end(); ++it) {
            SCOPE_ANR_AUTO(anr_timeout);
            uint64_t timestart = ::clock_app_monotonic();
            (*it).handler(messagewrapper->postid, messagewrapper->message);
            uint64_t timeend = ::clock_app_monotonic();
#if defined(DEBUG) && defined(__APPLE__)

            if (!isDebuggerPerforming())
#endif
                ASSERT2(0 >= anr_timeout || anr_timeout >= (int)(timeend - timestart), "anr_timeout:%d < cost:%llu, timestart:%llu, timeend:%llu", anr_timeout, timeend - timestart, timestart, timeend);
        }

        if (delmessage) {
            delete messagewrapper;
        }
    }
}

boost::shared_ptr<RunloopCond> RunloopCond::CurrentCond() {
    ScopedLock lock(sg_messagequeue_map_mutex);
    MessageQueue_t id = (MessageQueue_t)ThreadUtil::currentthreadid();
    
    if (sg_messagequeue_map.end() != sg_messagequeue_map.find(id)) {
        MessageQueueContent& content = sg_messagequeue_map[id];
        return content.breaker;
    } else {
        return boost::shared_ptr<RunloopCond>();
    }
}

MessageQueueCreater::MessageQueueCreater(bool _iscreate, const char* _msg_queue_name)
    : MessageQueueCreater(boost::shared_ptr<RunloopCond>(), _iscreate, _msg_queue_name)
{}
    
MessageQueueCreater::MessageQueueCreater(boost::shared_ptr<RunloopCond> _breaker, bool _iscreate, const char* _msg_queue_name)
    : thread_(boost::bind(&MessageQueueCreater::__ThreadRunloop, this), _msg_queue_name)
	, messagequeue_id_(KInvalidQueueID), breaker_(_breaker) {
	if (_iscreate)
		CreateMessageQueue();
}

MessageQueueCreater::~MessageQueueCreater() {
    CancelAndWait();
}

void MessageQueueCreater::__ThreadRunloop() {
    ScopedLock lock(messagequeue_mutex_);
    lock.unlock();
    
    RunLoop().Run();
    messagequeue_id_ = 0;
}

MessageQueue_t MessageQueueCreater::GetMessageQueue() {
    return messagequeue_id_;
}

MessageQueue_t MessageQueueCreater::CreateMessageQueue() {
    ScopedLock lock(messagequeue_mutex_);

    if (thread_.isruning()) return messagequeue_id_;

    if (0 != thread_.start()) { return KInvalidQueueID;}
    messagequeue_id_ = __CreateMessageQueueInfo(breaker_, thread_.tid());

    return messagequeue_id_;
}

void MessageQueueCreater::CancelAndWait() {
    ScopedLock lock(messagequeue_mutex_);

    if (KInvalidQueueID != messagequeue_id_) BreakMessageQueueRunloop(messagequeue_id_);

    lock.unlock();
    thread_.join();
}

MessageQueue_t MessageQueueCreater::CreateNewMessageQueue(boost::shared_ptr<RunloopCond> _breaker, const char* _messagequeue_name) {
    
    SpinLock* sp = new SpinLock;
    Thread thread(boost::bind(&__ThreadNewRunloop, sp), _messagequeue_name);
    thread.outside_join();
    ScopedSpinLock lock(*sp);

    if (0 != thread.start()) {
        delete sp;
        return KInvalidQueueID;
    }

    MessageQueue_t id = __CreateMessageQueueInfo(_breaker, thread.tid());
    return id;
}
    
MessageQueue_t MessageQueueCreater::CreateNewMessageQueue(const char* _messagequeue_name) {
    return CreateNewMessageQueue(boost::shared_ptr<RunloopCond>(), _messagequeue_name);
}
    
void MessageQueueCreater::ReleaseNewMessageQueue(MessageQueue_t _messagequeue_id){
    
    if (KInvalidQueueID == _messagequeue_id) return;
    
    BreakMessageQueueRunloop(_messagequeue_id);
    WaitForRuningLockEnd(_messagequeue_id);
    ThreadUtil::join((thread_tid)_messagequeue_id);
}

void MessageQueueCreater::__ThreadNewRunloop(SpinLock* _sp) {
    ScopedSpinLock lock(*_sp);
    lock.unlock();
    delete _sp;

    RunLoop().Run();
}

MessageQueue_t GetDefMessageQueue() {
    static MessageQueueCreater* s_defmessagequeue = new MessageQueueCreater;
    return s_defmessagequeue->CreateMessageQueue();
}

MessageQueue_t GetDefTaskQueue() {
    static MessageQueueCreater* s_deftaskqueue = new MessageQueueCreater;
    return s_deftaskqueue->CreateMessageQueue();
}

MessageHandler_t DefAsyncInvokeHandler(const MessageQueue_t& _messagequeue) {
    ScopedLock lock(sg_messagequeue_map_mutex);
    const MessageQueue_t& id = _messagequeue;

    if (sg_messagequeue_map.end() == sg_messagequeue_map.find(id)) return KNullHandler;

    MessageQueueContent& content = sg_messagequeue_map[id];
    return content.invoke_reg;
}

}  // namespace MessageQueue
