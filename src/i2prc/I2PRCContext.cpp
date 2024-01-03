#include <sys/stat.h>

#include <memory>
#include "api.h"
#include "Log.h"
#include "I2PRCContext.h"
#include "Transports.h"
#include "I2PRCService.h"

using boost::asio::ip::address;

namespace i2p
{
namespace client
{

constexpr uint8_t C2CP_MSG_SHOW_LEASESET = 1;
constexpr uint8_t C2CP_MSG_SHOW_TUNNELS = 2;
constexpr uint8_t C2CP_MSG_SHOW_STATUS = 3;
constexpr uint8_t C2CP_MSG_SEND_MESSAGE = 4;
constexpr uint8_t C2CP_MSG_UPLOAD_FILE = 5;
constexpr uint8_t C2CP_MSG_DOWNLOAD_FILE = 6;
constexpr uint8_t C2CP_MSG_EXEC_CMD = 7;
constexpr uint8_t C2CP_MSG_FILE_CONTENT = 8;
constexpr uint8_t C2CP_MSG_CREATE_SESSION = 9;




void I2PRCContext::Init(std::shared_ptr<I2PRCService> s, std::string addr, uint16_t port)
{
    m_Service = s;
    m_Acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(GetIOService(), boost::asio::ip::tcp::endpoint(address::from_string(addr), port));
    m_Service->Init(m_ReceiptQueue);
}

I2PRCContext::~I2PRCContext()
{
}


void I2PRCContext::Start()
{
    StartIOService();
    m_IsRunning = true;
    Accept();
    m_Service->Start();
}

void I2PRCContext::Stop()
{
}

void I2PRCContext::Accept()
{
    auto newSocket = std::make_shared<boost::asio::ip::tcp::socket>(GetIOService());
    auto callback = std::bind(&I2PRCContext::HandleAccept, this,
                              std::placeholders::_1, newSocket);
    m_Acceptor->async_accept(*newSocket, callback);
}

void I2PRCContext::HandleAccept(const boost::system::error_code &ecode,
                                std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!ecode && socket) {
        boost::system::error_code ec;
        auto ep = socket->remote_endpoint(ec);
        if (!ec) {
            LogPrint(eLogDebug, "I2CP: New connection from ", ep);
            m_IsConnect = true;
            HandleConnect(socket);
        } else {
            LogPrint(eLogError, "I2CP: Incoming connection error ",
                     ec.message());
        }
    }

    if (ecode != boost::asio::error::operation_aborted) {
        Accept();
    }
}

void I2PRCContext::HandleConnect(std::shared_ptr<boost::asio::ip::tcp::socket> sock)
{
    m_Socket = sock;
    m_ReceiptThread.reset(new std::thread([=](){
        while (m_IsConnect) {
            auto pack = m_ReceiptQueue->pull();
            uint8_t buf[C2P_BUFFER_SIZE + C2P_PACKET_HEADER_SIZE];
            buf[0] = pack.type;
            htobe32buf(buf + 1, pack.size);
            memcpy(buf + C2P_PACKET_HEADER_SIZE, pack.data, pack.size);
            auto realsend = m_Socket->send(boost::asio::buffer(buf, pack.size + C2P_PACKET_HEADER_SIZE));
            std::cout << "real send to cli " << realsend << " bytes" << std::endl;
        }
    }));
    GetIOService().post(std::bind(&I2PRCContext::HandlePacket, this));
}

void I2PRCContext::InsertHashMap(std::string key, std::string hash)
{
    m_HashMap.insert({key, hash});
}

std::string I2PRCContext::GetHash(std::string key)
{
    auto it = m_HashMap.find(key);
    if (it != m_HashMap.end()) {
        return it->second;
    }
    return "";
}



void I2PRCContext::ShowHashMap()
{
    for (const auto& it : m_HashMap) {
        std::cout << "Key: " << it.first << ", Value: " << it.second << std::endl;
    }
}



void I2PRCContext::Receive(size_t size)
{
    if (size <= 0) {
        return;
    }
    size_t ret;
    size_t offset = 0;
    while((ret = m_Socket->receive(boost::asio::buffer(m_Buffer + offset, size - offset))) > 0){
        offset += ret;
        if (offset == size)
            return;
    }
    std::cout << "cli loose connection" << std::endl;
}

uint8_t I2PRCContext::ReceivePacket(size_t& ret_data_size)
{
    Receive(C2P_PACKET_HEADER_SIZE);
    uint8_t type = m_Buffer[0];
    ret_data_size = bufbe32toh(m_Buffer + 1);
    Receive(ret_data_size);
    return type;
}


void I2PRCContext::HandlePacket() {
    while (m_IsRunning) {
        packet packet{};
        size_t size;
        uint8_t c2ptype = ReceivePacket(size);
        PACK_CMD_TYPE type = PACK_CMD_TYPE::NONE;
        switch (c2ptype) {
            case C2CP_MSG_CREATE_SESSION: {
                std::string ip(reinterpret_cast<char *>(m_Buffer));
                m_LastHash = GetHash(ip);
                std::cout << "session: " << m_LastHash << std::endl;
                break;
            }
            case C2CP_MSG_SHOW_LEASESET:
                HandleLocalInfo(ShowLeaseSetMessage());
                break;
            case C2CP_MSG_SHOW_TUNNELS:
                HandleLocalInfo(ShowTunnelsMessage());
                break;
            case C2CP_MSG_SHOW_STATUS:
                HandleLocalInfo(ShowStatusMessage());
                break;
            case C2CP_MSG_EXEC_CMD:
                type = PACK_CMD_TYPE::EXEC;
                break;
            case C2CP_MSG_SEND_MESSAGE:
                type = PACK_CMD_TYPE::MSG;
                break;
            case C2CP_MSG_UPLOAD_FILE:
                type = PACK_CMD_TYPE::INST;
                break;
            case C2CP_MSG_DOWNLOAD_FILE:
                type = PACK_CMD_TYPE::FILEDOWNLOAD;
                break;
            case C2CP_MSG_FILE_CONTENT:
                type = PACK_CMD_TYPE::FILESEND;
                break;
            default:
                break;
        }
        if (!m_LastHash.empty() && type != PACK_CMD_TYPE::NONE) {
            packet.type = type;
            packet.size = size;
            memcpy(packet.data, m_Buffer, packet.size);
            m_Service->Dispatch(m_LastHash, packet);
        }
        memset(m_Buffer, 0, C2P_BUFFER_SIZE);
    }
}

void I2PRCContext::HandleLocalInfo(std::string info)
{
    packet pack;
    pack.type = PACK_CMD_TYPE::MSG;
    pack.size = info.size();
    memcpy(pack.data, info.c_str(), pack.size);
    m_ReceiptQueue->push(pack);
}

std::string ShowLeaseSetMessage()
{
    std::stringstream s;
    s << "LeaseSets:\n\n";

    if (i2p::data::netdb.GetNumLeaseSets()) {
        int counter = 1;
        // for each lease set
        i2p::data::netdb.VisitLeaseSets(
            [&s, &counter](const i2p::data::IdentHash dest,
                           std::shared_ptr<i2p::data::LeaseSet> leaseSet) {
                // create copy of lease set so we extract leases
                auto storeType = leaseSet->GetStoreType();
                std::unique_ptr<i2p::data::LeaseSet> ls;
                if (storeType == i2p::data::NETDB_STORE_TYPE_LEASESET)
                    ls.reset(new i2p::data::LeaseSet(leaseSet->GetBuffer(),
                                                     leaseSet->GetBufferLen()));
                else {
                    ls.reset(new i2p::data::LeaseSet2(storeType));
                    ls->Update(leaseSet->GetBuffer(), leaseSet->GetBufferLen(),
                               false);
                }

                if (!ls) {
                    return;
                }

                s << dest.ToBase32() << "\n";
            });
    }
    return s.str();
}

std::string ShowTunnelsMessage()
{
    std::stringstream s;

    s << "Inbound tunnels:\n";
    for (auto &it : i2p::tunnel::tunnels.GetInboundTunnels()) {
        if (it->GetNumHops()) {
            it->VisitTunnelHops(
                [&s](std::shared_ptr<const i2p::data::IdentityEx> hopIdent) {
                    s << "-> "
                      << i2p::data::GetIdentHashAbbreviation(
                             hopIdent->GetIdentHash())
                      << " ";
                });
        }
        s << "-> " << it->GetTunnelID() << ":me";
        if (it->LatencyIsKnown()) {
            s << " ( " << it->GetMeanLatency() << "ms )";
        }
        s << "\r\n";
    }

    s << "\n"
      << "Outbound tunnels:\n";
    for (auto &it : i2p::tunnel::tunnels.GetOutboundTunnels()) {
        s << it->GetTunnelID() << ":me ->";
        // for each tunnel hop if not zero-hop
        if (it->GetNumHops()) {
            it->VisitTunnelHops(
                [&s](std::shared_ptr<const i2p::data::IdentityEx> hopIdent) {
                    s << " "
                      << i2p::data::GetIdentHashAbbreviation(
                             hopIdent->GetIdentHash())
                      << " ->";
                });
        }
        if (it->LatencyIsKnown()) {
            s << " ( " << it->GetMeanLatency() << "ms )";
        }

        s << "\r\n";
    }
    return s.str();
}

static void ShowTraffic(std::stringstream &s, uint64_t bytes)
{
    s << std::fixed << std::setprecision(2);
    auto numKBytes = (double)bytes / 1024;
    if (numKBytes < 1024) {
        s << numKBytes << " KiB";
    } else if (numKBytes < 1024 * 1024) {
        s << numKBytes / 1024 << " MiB";
    } else {
        s << numKBytes / 1024 / 1024 << " GiB";
    }
}

std::string ShowStatusMessage()
{
    std::stringstream s;
    s << "Status:\n\n";

    s << "Tunnel creation success rate: "
      << i2p::tunnel::tunnels.GetTunnelCreationSuccessRate() << "%\r\n";

    s << "Received:";
    ShowTraffic(s, i2p::transport::transports.GetTotalReceivedBytes());
    s << " "
      << "Sent: ";
    ShowTraffic(s, i2p::transport::transports.GetTotalSentBytes());
    s << " "
      << "Transit:";
    ShowTraffic(s,
                i2p::transport::transports.GetTotalTransitTransmittedBytes());
    s << "\r\n\r\n";

    s << "Router Ident: " << i2p::context.GetRouterInfo().GetIdentHashBase64()
      << "\r\n";
    s << "Router Caps: " << i2p::context.GetRouterInfo().GetProperty("caps")
      << "\r\n";
    s << "Our external address: \r\n";
    auto addresses = i2p::context.GetRouterInfo().GetAddresses();
    if (addresses) {
        for (const auto &address : *addresses) {
            if (!address)
                continue;
            switch (address->transportStyle) {
            case i2p::data::RouterInfo::eTransportNTCP2:
                s << "NTCP2";
                break;
            case i2p::data::RouterInfo::eTransportSSU2:
                s << "SSU2";
                break;
            default:
                s << "Unknown";
            }

            bool v6 = address->IsV6();
            if (v6) {
                if (address->IsV4())
                    s << "v4";
                s << "v6";
            }

            if (address->published)
                s << " " << (v6 ? "[" : "") << address->host.to_string()
                  << (v6 ? "]:" : ":") << address->port << "\r\n";
            else {
                if (address->port)
                    s << " :" << address->port;
                s << "\r\n";
            }
        }
    }

    s << "Routers: " << i2p::data::netdb.GetNumRouters() << " ";
    s << "Floodfills: " << i2p::data::netdb.GetNumFloodfills() << " ";
    s << "LeaseSets: " << i2p::data::netdb.GetNumLeaseSets() << "\r\n";

    size_t clientTunnelCount = i2p::tunnel::tunnels.CountOutboundTunnels();
    clientTunnelCount += i2p::tunnel::tunnels.CountInboundTunnels();
    size_t transitTunnelCount = i2p::tunnel::tunnels.CountTransitTunnels();

    s << "Client Tunnels: " << std::to_string(clientTunnelCount) << " ";
    s << "Transit Tunnels: " << std::to_string(transitTunnelCount)
      << "\r\n\r\n";
    return s.str();
}

I2PRCContext RCCTX;

}  // namespace client
}  // namespace i2p
