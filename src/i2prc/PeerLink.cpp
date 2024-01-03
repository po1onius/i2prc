#include <sys/stat.h>
#include "Log.h"
#include "api.h"
#include "Config.h"
#include "I2PEndian.h"
#include "PeerLink.h"
#include "RouterContext.h"



std::vector<std::string> SplitAddress(std::string addr_str, char delimiter)
{
    std::vector<std::string> addrs;

    std::stringstream ss(addr_str);
    std::string token;

    while (std::getline(ss, token, delimiter)) {
        addrs.push_back(token);
    }

    return addrs;
}

std::vector<std::string> SplitAddress(uint8_t *buf, uint32_t len, char delimiter)
{
    std::string tmp_str(reinterpret_cast<const char *>(buf), len);
    return SplitAddress(tmp_str, delimiter);
}

PreMeshnet::PreMeshnet(char* peers, std::string hash) : i2p::util::RunnableServiceWithWork("PreMeshnet"), m_IsRunning(true), m_Sock(GetIOService(), boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)), m_Hash(hash) {
    StartIOService();
    m_PeersIP = SplitAddress(peers, ',');
    m_BroadcastThread = std::make_unique<std::thread>(std::bind(&PreMeshnet::Broadcast, this));
}

std::map<std::string, ri_hash_buf> PreMeshnet::GetRouterInfo() {
    while (m_RIs.size() < m_PeersIP.size() - 1) {
        boost::asio::ip::udp::endpoint remote_addr;
        auto size = m_Sock.receive_from(boost::asio::buffer(m_Buffer, BUFMAXSIZE), remote_addr);
        auto ip = remote_addr.address().to_string();
        auto it = m_RIs.find(ip);
        if (it != m_RIs.end()) {
            continue;
        }
        ri_hash_buf rhb;
        rhb.risize = bufbe32toh(m_Buffer);
        memcpy(rhb.bufri, m_Buffer + 4, rhb.risize);
        rhb.hashsize = bufbe32toh(m_Buffer + 4 + rhb.risize);
        memcpy(rhb.hashbuf, m_Buffer + 4 + rhb.risize + 4, rhb.hashsize);
        std::cout << "receive other node ip: " << ip << std::endl;
        m_RIs[ip] = rhb;
        ri_hash_buf ri;
    }
    //while (m_RIs.size() < m_PeersIP.size())
    //    sleep(1);
    return m_RIs;
}

void PreMeshnet::Broadcast() {
    while (m_IsRunning) {
        for (auto& i : m_PeersIP) {
            using namespace boost::asio;
            uint8_t buf[BUFMAXSIZE];
            auto rilen = i2p::context.GetRouterInfo().GetBufferLen();
            htobe32buf(buf, rilen);
            memcpy(buf + 4, i2p::context.GetRouterInfo().GetBuffer(), rilen);
            htobe32buf(buf + 4 + rilen, m_Hash.size());
            memcpy(buf + 4 + rilen + 4, m_Hash.c_str(), m_Hash.size());
            m_Sock.send_to(buffer(buf, 4 + rilen + 4 + m_Hash.size()), ip::udp::endpoint(ip::address::from_string(i), port));
        }
        sleep(1);
    }
}

void PreMeshnet::Receive() {
    //m_Sock.async_receive_from(boost::asio::buffer(m_Buffer, RIMAXSIZE), m_sender, std::bind(&PreMeshnet::HandleReceive, this, std::placeholders::_1, std::placeholders::_2));
}

void PreMeshnet::HandleReceive(const boost::system::error_code &ecode,
                               std::size_t bytes_transferred) {
    ri_hash_buf ri;
    //memcpy(ri.buf, m_Buffer, bytes_transferred);
    //ri.size = bytes_transferred;
    //m_RIs.push_back(ri);
}

PreMeshnet::~PreMeshnet()
{
    m_BroadcastThread->join();
}