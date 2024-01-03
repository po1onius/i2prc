#ifndef __I2PRC_PEERLINK__
#define __I2PRC_PEERLINK__


static constexpr uint32_t port = 23232;
static constexpr uint32_t HASHMAXSIZE = 60;

struct ri_hash_buf {
    uint32_t risize;
    uint8_t bufri[i2p::data::MAX_RI_BUFFER_SIZE];
    uint32_t hashsize;
    uint8_t hashbuf[HASHMAXSIZE];
};
static constexpr uint32_t BUFMAXSIZE = sizeof(ri_hash_buf);

class PreMeshnet : public i2p::util::RunnableServiceWithWork
{
public:
    explicit PreMeshnet(char* peers, std::string hash);
    ~PreMeshnet();
    std::map<std::string, ri_hash_buf> GetRouterInfo();
private:
    void Broadcast();
    void Receive();
    void HandleReceive(const boost::system::error_code &ecode,
                       std::size_t bytes_transferred);
private:
    boost::asio::ip::udp::socket m_Sock;
    uint8_t m_Buffer[BUFMAXSIZE];
    std::vector<std::string> m_PeersIP;
    std::atomic<bool> m_IsRunning;
    std::map<std::string, ri_hash_buf> m_RIs;
    std::unique_ptr<std::thread> m_BroadcastThread;
    std::string m_Hash;
};


#endif