#ifndef __C2PSERVICE_H__
#define __C2PSERVICE_H__

#include <memory>
#include <string>
#include <fstream>
#include <ostream>
#include <inttypes.h>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <thread>
#include <mutex>

#include "util.h"
#include "Identity.h"
#include "RouterInfo.h"
#include <boost/thread/concurrent_queues/sync_queue.hpp>

namespace i2p
{
namespace client
{

constexpr uint64_t C2P_CONNECTION_MAX_IDLE = 3600;
constexpr size_t C2P_PACKET_HEADER_SIZE = 5;
constexpr size_t C2P_BUFFER_SIZE = 65536 - C2P_PACKET_HEADER_SIZE;

enum PACK_CMD_TYPE : uint8_t
{
    NONE,
    MSG,
    ASK,
    INST,
    PTY,
    FILEDOWNLOAD,
    FILEPASSIVESEND,
    FILESEND,
    FILESAVE,
    EXEC,
    EXECRET,
};



struct packet
{
    PACK_CMD_TYPE type;
    uint32_t size;
    uint8_t data[C2P_BUFFER_SIZE];
};


class C2PServiceHandler;
class I2PRCService : public std::enable_shared_from_this<I2PRCService>
{
public:
    I2PRCService(std::shared_ptr<i2p::client::ClientDestination> dest) : m_Dest(dest) {};
    ~I2PRCService();
    void Init(std::shared_ptr<boost::concurrent::sync_queue<packet>> ReceiptRef);
    void Start();
    void Dispatch(std::string hash, packet pack);
    void Receipt(uint8_t type, uint8_t* buf, size_t size);

    inline void AddHandler(std::string hat, std::shared_ptr<C2PServiceHandler> conn)
    {
        std::unique_lock<std::mutex> l(m_HandlersMutex);
        m_Handlers.insert(std::pair<std::string, std::shared_ptr<C2PServiceHandler>>(hat, conn));
    }
    inline void RemoveHandler(std::shared_ptr<C2PServiceHandler> conn)
    {
        std::unique_lock<std::mutex> l(m_HandlersMutex);
        for (auto it = m_Handlers.begin(); it != m_Handlers.end(); it++) {
            if (it->second == conn) {
                m_Handlers.erase(it);
                break;
            }
        }
    }
    std::shared_ptr<C2PServiceHandler> CreateHandler(std::shared_ptr<i2p::stream::Stream> s, PACK_CMD_TYPE type);
    std::shared_ptr<C2PServiceHandler> CreateHandler(std::string hash, PACK_CMD_TYPE type);

private:
    std::shared_ptr<C2PServiceHandler> GetHandler(std::string hash, PACK_CMD_TYPE type);
    void HandleAccept(std::shared_ptr<i2p::stream::Stream> s);


private:
    std::shared_ptr<i2p::client::ClientDestination> m_Dest;
    std::map<std::string, std::shared_ptr<C2PServiceHandler>> m_Handlers;
    std::mutex m_HandlersMutex;
    std::shared_ptr<boost::concurrent::sync_queue<packet>> m_ReceiptQueue;

};

class C2PServiceHandler : public i2p::util::RunnableServiceWithWork, public std::enable_shared_from_this<C2PServiceHandler> {
public:
    typedef std::function<void (size_t)> FileHandler;
    C2PServiceHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner);
    virtual ~C2PServiceHandler();
    virtual void Handle(uint8_t* buf ,size_t size) = 0;

    virtual void HandleStreamReceive(const boost::system::error_code &ecode,
                              std::size_t bytes_transferred){};
    virtual void HandleStreamSend(const boost::system::error_code &ecode){};

    void Done();
    void StreamReceive();
    void StreamSend(size_t len);
protected:
    std::shared_ptr<I2PRCService> m_Owner;
    uint8_t m_StreamBuffer[C2P_BUFFER_SIZE];
    std::shared_ptr<i2p::stream::Stream> m_Stream;
};



class C2PMsgHandler : public C2PServiceHandler
{
public:
    C2PMsgHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PServiceHandler(s, owner) {};
    void Handle(uint8_t* buf, size_t size) override;
    void SendCMD(PACK_CMD_TYPE type, uint8_t* cmd, size_t size);
protected:
};

class C2PInstHandler : public C2PMsgHandler
{
public:
    C2PInstHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PMsgHandler(s, owner) {};
    void Handle(uint8_t* buf, size_t size) override;
};


class C2PAskHandler : public C2PMsgHandler
{
public:
    C2PAskHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PMsgHandler(s, owner) {};
    void HandleStreamReceive(const boost::system::error_code &ecode,
                             std::size_t bytes_transferred) override;
    void Handle(uint8_t* buf, size_t size) override;
};

class C2PMsgReplyHandler : public C2PServiceHandler
{
public:
    C2PMsgReplyHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PServiceHandler(s, owner) {};
    void Handle(uint8_t* buf, size_t size);
    void ReplyMsg();
    virtual int GetReply();

protected:
    std::string m_ReceiveMsg;
};

class C2PCMDHandler : public C2PMsgReplyHandler
{
public:
    C2PCMDHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PMsgReplyHandler(s, owner) {};
    int GetReply() final;
};


class C2PFileDownloadHandler : public C2PMsgHandler
{
public:
    C2PFileDownloadHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PMsgHandler(s, owner), m_SizeSent(false) {};
    void Handle(uint8_t*buf, size_t size) override;
    void HandleStreamReceive(const boost::system::error_code &ecode,
                             std::size_t bytes_transferred) override;
private:
    bool m_SizeSent;
};

class C2PFileTransPassiveSendHandler : public C2PServiceHandler
{
public:
    C2PFileTransPassiveSendHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PServiceHandler(s, owner), m_FileSize(0) {};
    void Handle(uint8_t* buf, size_t size) override;
    void HandleStreamSend(const boost::system::error_code &ecode) override;
private:
    void ReadFromFile();
private:
    uint32_t m_FileSize;
    std::ifstream m_File;
};

class C2PFileTransSendHandler : public C2PServiceHandler
{
public:
    C2PFileTransSendHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PServiceHandler(s, owner) {};
    virtual ~C2PFileTransSendHandler();
    void Handle(uint8_t* buf, size_t size) override;
private:

};

class C2PFileTransSaveHandler : public C2PServiceHandler
{
public:
    //using FileHandler = std::function<void (size_t size)>;
    C2PFileTransSaveHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : C2PServiceHandler(s, owner) {};
    void HandleStreamReceive(const boost::system::error_code &ecode,
                                     std::size_t bytes_transferred) override;
    void Handle(uint8_t* buf, size_t size) override;
    void HandleWritenToFile(size_t size);
private:
    void WriteToFile(uint8_t* buf, size_t size, FileHandler handler);
private:
    uint64_t m_Filesize;
    std::ofstream m_File;
};


}  // namespace client
}  // namespace i2p

#endif
