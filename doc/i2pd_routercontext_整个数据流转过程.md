## RouterContext

该模块的主要采取定时器的触发形式,定时更新路由信息,租约等信息

路由器拥有两种模式

1.隐藏模式

2.显式模式

两种模式的区别为是否更新路由信息,即是否会向周围路由器发送自身已知的的路由信息

### 1.初始化定时器,创建发布路由信息事件

```
m_PublishTimer.reset (new boost::asio::deadline_timer (m_Service->GetService ()));
				ScheduleInitialPublish ();
```

### 2.定时器到期,更新自身netdb 发布路由信息

条件:确定当前路由是否对外可访问

方法,是检查路由信息是否可以通过所有传输方式进行访问。

(具体的传输判断在routeinfo.cpp中,逻辑流程未知)

```
void RouterContext::HandlePublishTimer (const boost::system::error_code& ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			m_PublishExcluded.clear ();
			m_PublishReplyToken = 0;
			if (IsFloodfill ())
			{
				UpdateStats (); // for floodfill //更新netdb的租约信息,并将各个路由信息文件落地
				m_PublishExcluded.insert (i2p::context.GetIdentHash ()); // don't publish to ourselves//特殊的hash表,防止自身收到自己发出的路由信息
			}
		UpdateTimestamp (i2p::util::GetSecondsSinceEpoch ());
			Publish ();//负责将路由器信息发布到洪泛填充网络中
			SchedulePublishResend ();//重新设置定时器
		}
	}
```

#### 2.1 路由信息发布到最近洪泛节点

1.查询最近的洪泛节点,除去特殊的hash

`auto floodfill = i2p::data::netdb.GetClosestFloodfill (i2p::context.GetIdentHash (), m_PublishExcluded);`

2.组建路由信息包

```
struct
{
   uint32_t *dest_routerhash (32)
   uint32_t a = 0x00
   uint32_t replytoken;(随机生成)
   uint16_t router_info_len
   uint32_t *router_info (当长度小于940经过无压缩gzip,大于则采取zlib库进行压缩)
}

struct router_info
{
   char gzipheader[11];(固定 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x01)
   uint16_t original_len
   uint16_t 0xffff-original_len(剩于长度)
   uint32_t crc
   uint32_t buf_len
}
```

#### 2.2协议选择

`bool Transports::ConnectToPeer (const i2p::data::IdentHash& ident, Peer& peer)`

* 首先检查 `peer` 是否已经具有 `router`（路由器信息），如果没有，则重新设置 `peer` 的路由器信息，通过调用 `netdb.FindRouter(ident)` 方法尝试从网络数据库中获取新的路由器信息。
* 如果 `peer` 具有路由器信息，则继续执行以下操作：
  * 如果 `peer.priority` 为空，则调用 `SetPriority(peer)` 方法设置对等方的优先级。
  * 在循环中，逐个尝试 `peer.priority` 中的传输方式（transport）：
    * 如果是 `NTCP2V4` 或 `NTCP2V6` 传输方式：
      * 如果 `m_NTCP2Server` 为空，则继续下一次循环。
      * 获取对等方路由器的相应地址，并根据地址是否在保留范围内进行判断。
      * 如果地址存在，则创建一个 `NTCP2Session` 对象，并根据是否使用代理来进行连接。
      * 返回 `true` 表示连接成功。
    * 如果是 `SSU2V4` 或 `SSU2V6` 传输方式：
      * 如果 `m_SSU2Server` 为空，则继续下一次循环。
      * 获取对等方路由器的相应地址，并根据地址是否在保留范围内进行判断。
      * 如果地址存在且是可达的，则尝试创建一个 `SSU` 会话。
      * 返回 `true` 表示连接成功。
    * 如果是 `NTCP2V6Mesh` 传输方式：
      * 如果 `m_NTCP2Server` 为空，则继续下一次循环。
      * 获取对等方路由器的 Yggdrasil 地址。
      * 如果地址存在，则创建一个 `NTCP2Session` 对象，并进行连接。
      * 返回 `true` 表示连接成功。
    * 如果是其他未知的传输方式，则记录错误信息。
  * 如果循环结束后仍未成功连接，则记录错误信息，并根据对等方是否可达来设置对等方在网络数据库中的可达性状态。
  * 完成对等方的处理，从 `m_Peers` 中移除对等方，并返回 `false` 表示连接失败。
* 如果 `peer` 没有路由器信息，则请求对等方的路由器信息。
  * 记录日志信息表示正在请求对等方的路由器信息。
  * 调用 `netdb.RequestDestination` 方法请求对等方的路由器信息，并在请求完成时调用 `RequestComplete` 方法。
* 返回 `true` 表示连接正在进行中。

#### 2.2 传输方式均不可达

* 首先，使用 `tunnels.GetExploratoryPool()` 获取一个探索性隧道池（`exploratoryPool`）。
* 然后，分别使用 `exploratoryPool` 获取下一个出站隧道（`outbound`）和下一个入站隧道（`inbound`）。通过调用 `GetNextOutboundTunnel` 和 `GetNextInboundTunnel` 方法来获取隧道对象。
* 如果 `inbound` 和 `outbound` 都存在，则执行以下操作：
  * 调用 `outbound` 的 `SendTunnelDataMsgTo` 方法，将一个创建的数据库存储消息发送给指定的洪水填充节点（`floodfill->GetIdentHash()`）。
  * 创建数据库存储消息使用了 `CreateDatabaseStoreMsg` 方法，传入了当前路由器的共享路由器信息（`i2p::context.GetSharedRouterInfo()`）、回复令牌（`replyToken`）和 `inbound` 对象。
* 如果 `inbound` 和 `outbound` 之中至少有一个不存在，则记录一条日志，表示无法发布路由器信息，并指定在多少秒后再次尝试。

最后，将洪水填充节点的身份散列值（`floodfill->GetIdentHash()`）添加到 `m_PublishExcluded` 集合中，并将回复令牌（`replyToken`）赋值给 `m_PublishReplyToken`。

### 3. 阻塞更新路由信息

```
void RouterContext::HandleCongestionUpdateTimer (const boost::system::error_code& ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto c = i2p::data::RouterInfo::eLowCongestion;
			if (!AcceptsTunnels ()) 
				c = i2p::data::RouterInfo::eRejectAll;
			else if (IsHighCongestion ()) 
				c = i2p::data::RouterInfo::eHighCongestion;
			if (m_RouterInfo.UpdateCongestion (c))
				UpdateRouterInfo ();
			ScheduleCongestionUpdate ();
		}
	}
```

* 首先，检查错误代码 `ecode` 是否等于 `boost::asio::error::operation_aborted`，如果不等于则执行以下操作：
  * 根据路由器的接受隧道能力来确定拥塞级别 `c`。如果路由器不接受隧道，则设置 `c` 为 `i2p::data::RouterInfo::eRejectAll`；如果路由器处于高拥塞状态，则设置 `c` 为 `i2p::data::RouterInfo::eHighCongestion`；否则，设置 `c` 为 `i2p::data::RouterInfo::eLowCongestion`。
  * 调用路由器信息对象 `m_RouterInfo` 的 `UpdateCongestion` 方法，将拥塞级别 `c` 更新到路由器信息中。如果更新成功，则调用 `UpdateRouterInfo` 方法更新路由器信息。
  * 调用 `ScheduleCongestionUpdate` 方法，以安排下一次拥塞更新。

### tunnel数据的方式处理

#### 1.数据经ssu2\ntcp2等数据发送到路由器端

(具体的session,数据组包过程未进行具体的分析)

#### 2.数据类型判断

根据交付指令的数据头判断该数据的数据类型

```
case eSSU2BlkI2NPMessage:
				{
					LogPrint (eLogDebug, "SSU2: I2NP message");
					auto nextMsg = (buf[offset] == eI2NPTunnelData) ? NewI2NPTunnelMessage (true) : NewI2NPShortMessage ();
					nextMsg->len = nextMsg->offset + size + 7; // 7 more bytes for full I2NP header
					memcpy (nextMsg->GetNTCP2Header (), buf + offset, size);
					nextMsg->FromNTCP2 (); // SSU2 has the same format as NTCP2
					HandleI2NPMsg (std::move (nextMsg));
					m_IsDataReceived = true;
					break;
				}
```

根据上述组包,本次分析为(ei2npTunnelData)数据

#### 3.放入I2NP的typeid各个类型的消息队列中等待处理

```
	void I2NPMessagesHandler::PutNextMessage (std::shared_ptr<I2NPMessage>&& msg)
	{
		if (msg)
		{
			switch (msg->GetTypeID ())
			{
				case eI2NPTunnelData:
					m_TunnelMsgs.push_back (msg);
				break;
				case eI2NPTunnelGateway:
					m_TunnelGatewayMsgs.push_back (msg);
				break;
				default:
					HandleI2NPMessage (msg);
			}
		}
	}
```

#### 4.等待数据接受完毕,将消息缓存队列中的数据放入数据处理队列

```
	void HandleI2NPMessage (std::shared_ptr<I2NPMessage> msg)
	{
		if (msg)
		{
			uint8_t typeID = msg->GetTypeID ();
			LogPrint (eLogDebug, "I2NP: Handling message with type ", (int)typeID);
			switch (typeID)
			{
				case eI2NPTunnelData:
					i2p::tunnel::tunnels.PostTunnelData (msg);
				break;
				case eI2NPTunnelGateway:
					i2p::tunnel::tunnels.PostTunnelData (msg);
				break;
				case eI2NPGarlic:
				{
					if (msg->from && msg->from->GetTunnelPool ())
						msg->from->GetTunnelPool ()->ProcessGarlicMessage (msg);
					else
						i2p::context.ProcessGarlicMessage (msg);
					break;
				}
				case eI2NPDatabaseStore:
				case eI2NPDatabaseSearchReply:
				case eI2NPDatabaseLookup:
					// forward to netDb
					i2p::data::netdb.PostI2NPMsg (msg);
				break;
				case eI2NPDeliveryStatus:
				{
					if (msg->from && msg->from->GetTunnelPool ())
						msg->from->GetTunnelPool ()->ProcessDeliveryStatus (msg);
					else
						i2p::context.ProcessDeliveryStatusMessage (msg);
					break;
				}
				case eI2NPVariableTunnelBuild:
				case eI2NPVariableTunnelBuildReply:
				case eI2NPTunnelBuild:
				case eI2NPTunnelBuildReply:
				case eI2NPShortTunnelBuild:
				case eI2NPShortTunnelBuildReply:
					// forward to tunnel thread
					i2p::tunnel::tunnels.PostTunnelData (msg);
				break;
				default:
					HandleI2NPMessage (msg->GetBuffer (), msg->GetLength ());
			}
		}
	}

```

根据据类型发送给netdb处理 

后续该处理处理,根据数据包携带的回复tunelid进行回复
