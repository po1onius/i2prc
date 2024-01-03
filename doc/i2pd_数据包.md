### 1. 1数据包结构(I2NP)

隧道头部存储随机初始向量、计算校验和、填充数,长度1003,除了特定的几位后续全部随机填充

交付指令由tunnel头组成

i2np有头部+2(保留)+负载组成

总的隧道数据(隧道头部+交付指令+(i2np)负载+2字节长度)

#### 1.1.0.1发送时创建一个新的I2NP隧道消息对象(隧道头部+交付指令+(i2np)负载+2字节长度)

(从内存池取, 区别长度对齐 6/12 偏移量+6/无)

总长度2106/1078

如果endpoint为true，则表示该隧道消息是从隧道终点发送的，因此需要预留足够的空间来容纳TunnelGateway头和加密后的流式数据包。在这种情况下，该方法使用m_I2NPTunnelEndpointMessagesMemoryPool内存池来获取隧道消息对象，并对其进行对齐和调整以确保有足够的空间容纳数据。

如果endpoint为false，则表示该隧道消息是从隧道中转发送的，因此只需要使用m_I2NPTunnelMessagesMemoryPool内存池来获取隧道消息对象，并对其进行对齐。

##### //交付指令(di)

di_len=43

if(隧道传递)

di[0] = `block.deliveryType`

di[1] = tunnelID

di[5-37] = block.hash

if(路由传递)

di[1-33]=block.hash

`m_CurrentTunnelDataMsg->buf`的尾部+di+i2np数据

`CompleteCurrentTunnelDataMessage`() //该方法的目的是对隧道数据消息进行一些处理，包括生成随机初始向量、计算校验和、填充数据等。这些操作是为了确保数据的安全性和完整性，并为后续的加密操作做准备。(没看)(头部存储随机初始向量、计算校验和、填充数,长度1003,除了特定的几位后续全部随机填充)

I2NP数据加密 `m_Tunnel->EncryptTunnelMsg (tunnelMsg, newMsg);`使用前面的向量等进行加密

后续发送...

#### 1.1.0.2 tunnel头

```
struct TunnelMessageBlock {
        TunnelDeliveryType deliveryType;
        i2p::data::IdentHash hash;
        uint32_t tunnelID;
        std::shared_ptr<I2NPMessage> data;
    };
```

```
enum TunnelDeliveryType
	{
		eDeliveryTypeLocal = 0,
		eDeliveryTypeTunnel = 1,
		eDeliveryTypeRouter = 2
	};
* `eDeliveryTypeLocal`表示消息将直接传递到本地，不通过任何隧道。
* `eDeliveryTypeTunnel`表示消息将通过隧道进行传递，需要指定隧道ID。
* `eDeliveryTypeRouter`表示消息将通过路由器进行传递，具体的传递路径由路由器决定。
```

#### 1.1.1  I2NP头部

`I2NP_HEADER_SIZE `长度 16+2(保留)
`I2NPMessage`


| 0               | 1               | 2 | 3 | 4 | 5                     | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13             | 14 | 15  | 16 | 17 |
| ----------------- | ----------------- | --- | --- | --- | ----------------------- | --- | --- | --- | --- | ---- | ---- | ---- | ---------------- | ---- | ----- | ---- | ---- |
| (uint8_t)TypeID | (uint32_t)MSGID |   |   |   | (uint64_t) expiration |   |   |   |   |    |    |    | (uint16_t)size |    | cks | /  | /  |

#### 1.1.2数据包各个部分分析

##### 0.标识判断(TypelD)

`msg->GetTypeID`

```
libi2pd/I2NPProtocol.h
enum I2NPMessageType
	{
		eI2NPDummyMsg = 0,
		eI2NPDatabaseStore = 1,
		eI2NPDatabaseLookup = 2,
		eI2NPDatabaseSearchReply = 3,
		eI2NPDeliveryStatus = 10,
		eI2NPGarlic = 11,
		eI2NPTunnelData = 18,
		eI2NPTunnelGateway = 19,
		eI2NPData = 20,
		eI2NPTunnelBuild = 21,
		eI2NPTunnelBuildReply = 22,
		eI2NPVariableTunnelBuild = 23,
		eI2NPVariableTunnelBuildReply = 24,
		eI2NPShortTunnelBuild = 25,
		eI2NPShortTunnelBuildReply = 26
	};

* eI2NPDummyMsg：虚拟消息，用于占位或测试目的。
* eI2NPDatabaseStore：数据库存储消息，用于将数据存储到I2P网络中的数据库中。
* eI2NPDatabaseLookup：数据库查找消息，用于在I2P网络中的数据库中查找数据。
* eI2NPDatabaseSearchReply：数据库搜索回复消息，用于回复数据库查找请求并提供相应的数据。
* eI2NPDeliveryStatus：传输状态消息，用于报告消息的传输状态，例如成功发送或发送失败。
* eI2NPGarlic：大蒜消息，用于在I2P网络中进行匿名通信。
* eI2NPTunnelData：隧道数据消息，用于在I2P网络中传输经过隧道封装的数据。
* eI2NPTunnelGateway：隧道网关消息，用于在I2P网络中建立和管理隧道。
* eI2NPData：数据消息，用于在I2P网络中传输普通数据。
* eI2NPTunnelBuild：隧道建立消息，用于请求建立一个新的隧道。
* eI2NPTunnelBuildReply：隧道建立回复消息，用于回复隧道建立请求并提供相关信息。
* eI2NPVariableTunnelBuild：可变长度隧道建立消息，用于请求建立一个可变长度的隧道。
* eI2NPVariableTunnelBuildReply：可变长度隧道建立回复消息，用于回复可变长度隧道建立请求并提供相关信息。
* eI2NPShortTunnelBuild：短隧道建立消息，用于请求建立一个短隧道。
* eI2NPShortTunnelBuildReply：短隧道建立回复消息，用于回复短隧道建立请求并提供相关信息。
```

##### 1.消息ID(MSGID)

msgID（消息ID）是一个重要的组成部分，用于在网络中唯一标识一个消息。分包使用

##### 5.数据存活期限(expiration)

##### 13.数据负载长度(size)

##### 15.数据负载的hash值(cks)
