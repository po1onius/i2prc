#ifndef __C2CP_H__
#define __C2CP_H__

#include <inttypes.h>
#include <string>
#include <memory>
#include <thread>
#include <map>
#include <boost/asio.hpp>
#include "util.h"
#include "I2PRCService.h"
#include "Destination.h"


namespace i2p
{
namespace client
{

struct Packet;
class I2PRCContext : public i2p::util::RunnableServiceWithWork
{

public:
    I2PRCContext() : i2p::util::RunnableServiceWithWork("I2PRCContext"), m_ReceiptQueue(std::make_shared<boost::concurrent::sync_queue<packet>>()) {};
    void Init(std::shared_ptr<I2PRCService> s, std::string addr, uint16_t port);
    ~I2PRCContext();

    void Start();
    void Stop();

    void Receive(size_t size = C2P_BUFFER_SIZE);
    uint8_t ReceivePacket(size_t& ret_data_size);
    void HandlePacket();
    void HandleLocalInfo(std::string info);

    void InsertHashMap(std::string key, std::string hash);
    std::string GetHash(std::string key);
    void ShowHashMap();
private:
    std::shared_ptr<boost::asio::ip::tcp::acceptor> m_Acceptor;
    std::unordered_map<std::string, std::string > m_HashMap;
    std::string m_LastHash;
    uint8_t m_Buffer[C2P_BUFFER_SIZE];
    std::shared_ptr<boost::asio::ip::tcp::socket> m_Socket;
    std::shared_ptr<I2PRCService> m_Service;
    std::unique_ptr<std::thread> m_ReceiptThread;

    std::shared_ptr<boost::concurrent::sync_queue<packet>> m_ReceiptQueue;
    std::atomic<bool> m_IsConnect;
    std::atomic<bool> m_IsRunning;
private:
    void Accept();
    void HandleConnect(std::shared_ptr<boost::asio::ip::tcp::socket> sock);
    void HandleAccept(const boost::system::error_code &ecode,
                      std::shared_ptr<boost::asio::ip::tcp::socket> socket);

};


std::string ShowLeaseSetMessage();
std::string ShowTunnelsMessage();
std::string ShowStatusMessage();
extern I2PRCContext RCCTX;

}  //namespace client
}  // namespace i2p
#endif
