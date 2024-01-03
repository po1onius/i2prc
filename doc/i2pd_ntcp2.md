## NTCP2

目前仅分析直连的情况

(SOCKS5 HTTP PROXY MESH未进行分析记录)

### 使用方式

1.接受端,在接受链接后,会创建一个会话以及会话属性,后解析发送端的请求会话请求,对于会话请求进行解密,过程使用了CBC模式的加密算法，并使用本地的身份哈希值作为密钥，以及NTCP2的初始化向量（IV）作为初始向量（IV）。验证通过后,才会发送会话许可(分为3中情况,1,会话请求时间超时,2,未有附加数据,3.有附加数据),否则将关闭此会话

### server端

#### 生成密钥对

`	m_X25519KeysPairSupplier.Start ();`

#### 异步接受连接

`m_NTCP2Acceptor->async_accept(conn->GetSocket (), std::bind (&NTCP2Server::HandleAccept, this, conn, std::placeholders::_1));`

#### 连接信息处理

```
void NTCP2Server::HandleAccept (std::shared_ptr<NTCP2Session> conn, const boost::system::error_code& error)
	{
		if (!error && conn)
		{
			boost::system::error_code ec;
			auto ep = conn->GetSocket ().remote_endpoint(ec);
			if (!ec)
			{
				LogPrint (eLogDebug, "NTCP2: Connected from ", ep);
				if (!i2p::util::net::IsInReservedRange(ep.address ()))
				{
					if (m_PendingIncomingSessions.emplace (ep.address (), conn).second)
					{
						conn->SetRemoteEndpoint (ep);
						conn->ServerLogin ();
						conn = nullptr;
					}
					else
						LogPrint (eLogInfo, "NTCP2: Incoming session from ", ep.address (), " is already pending");
				}
				else
					LogPrint (eLogError, "NTCP2: Incoming connection from invalid IP ", ep.address ());
			}
			else
				LogPrint (eLogError, "NTCP2: Connected from error ", ec.message ());
		}
		else
		{
			LogPrint (eLogError, "NTCP2: Accept error ", error.message ());
			if (error == boost::asio::error::no_descriptors)
			{
				i2p::context.SetError (eRouterErrorNoDescriptors);
				return;
			}
		}

		if (error != boost::asio::error::operation_aborted)
		{
			if (!conn) // connection is used, create new one
				conn = std::make_shared<NTCP2Session> (*this);
			else // reuse failed
				conn->Close ();
			m_NTCP2Acceptor->async_accept(conn->GetSocket (), std::bind (&NTCP2Server::HandleAccept, this,
				conn, std::placeholders::_1));
		}
	}
```

`HandleAccept`函数用于处理接受到的NTCP2连接。它接受两个参数：`std::shared_ptr<NTCP2Session> conn`表示接受到的连接对象，`const boost::system::error_code& error`表示接受连接操作的错误码。

在函数体内，首先检查 `error`是否为空且 `conn`是否存在。如果满足条件，则表示连接成功接受。

接下来，通过调用 `conn->GetSocket().remote_endpoint(ec)`，获取连接的远程端点（即对方的IP地址和端口号），并将其赋值给 `ep`变量。

如果获取远程端点的操作没有错误（即 `!ec`），则进行一系列处理，包括记录调试日志、检查IP地址是否在保留范围内、将连接对象添加到待处理的传入会话列表中、设置连接的远程端点、进行服务器登录等。

(`m_PendingIncomingSessions.emplace(ep.address(), conn)`的作用是将 `ep.address()`作为键，`conn`作为值插入到容器中。`emplace`函数返回一个 `std::pair`对象，其中的 `second`成员表示插入操作是否成功。

* 调用 `conn->SetRemoteEndpoint(ep)`，设置连接对象的远程端点
* 调用 `conn->ServerLogin()`，进行服务器登录
* 将 `conn`置为空指针，以便避免继续使用该连接对象)

如果获取远程端点的操作出现错误，将记录错误日志。

然后，根据错误码进行处理。如果错误码是 `boost::asio::error::no_descriptors`，表示没有可用的描述符，此时会设置路由器错误并返回。

最后，如果错误码不是 `boost::asio::error::operation_aborted`，则需要继续接受连接。如果 `conn`为空（表示连接已经被使用），则创建一个新的连接对象；否则，关闭连接对象。然后，通过调用 `m_NTCP2Acceptor->async_accept`函数再次异步接受连接，并传入适当的参数。

总的来说，`HandleAccept`函数的作用是处理接受到的NTCP2连接，包括记录日志、检查IP地址、管理连接对象等。

#### 会话创建,设置会话属性

`	void NTCP2Session::ServerLogin ()`

`ServerLogin`函数通过调用 `m_Establisher->CreateEphemeralKey()`来创建一个临时的密钥对，创建临时密钥对是在进行NTCP2服务器登录时的一个重要步骤，用于确保连接的安全性和身份验证。

除了创建临时密钥对外，`ServerLogin`函数还进行了其他一些操作，如设置超时时间、更新活动时间戳以及异步读取会话请求数据。这些操作都是登录过程中的一部分，用于确保连接的顺利建立和进行进一步的通信。

#### 处理会话请求消息解密验证

`bool NTCP2Establisher::ProcessSessionRequestMessage (uint16_t& paddingLen, bool& clockSkew)`

通过使用对称加密算法解密 `m_SessionRequestBuffer`中的数据。解密过程使用了CBC模式的加密算法，并使用本地的身份哈希值作为密钥，以及NTCP2的初始化向量（IV）作为初始向量（IV）。

生成下一个数据块的解密密钥，并使用该密钥对MAC进行验证，并解密选项块（32字节）。解密过程使用了AEADChaCha20Poly1305算法。

在解密完成后，对解密后的选项进行验证和处理：

* 检查选项中的网络ID是否与本地的网络ID匹配，如果不匹配，则返回false。
* 检查选项中的版本号是否为2，如果不是，则返回false。
* 获取填充长度和m3p2长度，并进行相应的检查。
* 检查时间戳是否在合理范围内，如果超出了时钟偏移值的范围，则设置 `clockSkew`为 `true`，表示时钟偏移值超出限制。

如果以上验证和处理都通过，则返回true。否则，返回false。

验证不通过,则会关闭当前会话

#### 异步接收请求数据,并处理接受到的数据(作者未对接受到数据进行处理)

```
	boost::asio::async_read (m_Socket, boost::asio::buffer(m_Establisher->m_SessionRequestBuffer + 64, paddingLen), boost::asio::transfer_all (),
							std::bind(&NTCP2Session::HandleSessionRequestPaddingReceived, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
```

读取的数据将被存储在 `m_Establisher->m_SessionRequestBuffer + 64`的缓冲区中，读取的数据长度为 `paddingLen`。

在读取操作完成后，将调用 `NTCP2Session::HandleSessionRequestPaddingReceived`函数来处理接收到的数据。

#### 发送会话许可

```
void NTCP2Session::SendSessionCreated ()
	{
		m_Establisher->CreateSessionCreatedMessage ();
		// send message
		m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch ();
		boost::asio::async_write (m_Socket, boost::asio::buffer (m_Establisher->m_SessionCreatedBuffer, m_Establisher->m_SessionCreatedBufferLen), boost::asio::transfer_all (),
			std::bind(&NTCP2Session::HandleSessionCreatedSent, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
	}
```

`CreateSessionCreatedMessage`函数的作用是创建 `SessionCreated`消息的内容，包括填充数据、加密公钥、生成选项数组并进行签名和加密。

#### 确认会话发送成功,接收会话确认消息,完成密钥 签名的认证

`	void NTCP2Session::HandleSessionConfirmedReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred)`

##### 1.首先只接收routerinfo信息

```
if (buf[0] != eNTCP2BlkRouterInfo)
					{
						LogPrint (eLogWarning, "NTCP2: Unexpected block ", (int)buf[0], " in SessionConfirmed");
						Terminate ();
						return;
					}
```

##### 2.将routerinfo信息发送给netdb处理,netdb会根据该routerinfo信息发送给其他节点

`	i2p::data::netdb.PostI2NPMsg (CreateI2NPMessage (eI2NPDummyMsg, buf.data () + 3, size)); // TODO: should insert ri and not parse it twice`

##### 3.通过底层ransport layer与对端建立了链接

使用 `m_Service`对象的 `post`函数将后续的处理放入传输层所在的服务（service）的事件队列中

`void Transports::PeerConnected (std::shared_ptr<TransportSession> session)`

#### 接受数据并进行处理(分包进行合包后一次性处理)

(在Linux系统中，它使用 `setsockopt`函数设置 `TCP_QUICKACK`选项为1，表示启用快速ACK（Acknowledgement）机制,具体是怎么还不清楚)

`void NTCP2Session::HandleReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred)`

#### 具体的数据处理

```
void NTCP2Session::ProcessNextFrame (const uint8_t * frame, size_t len)
	{
		size_t offset = 0;
		while (offset < len)
		{
			uint8_t blk = frame[offset];
			offset++;
			auto size = bufbe16toh (frame + offset);
			offset += 2;
			LogPrint (eLogDebug, "NTCP2: Block type ", (int)blk, " of size ", size);
			if (size > len)
			{
				LogPrint (eLogError, "NTCP2: Unexpected block length ", size);
				break;
			}
			switch (blk)
			{
				case eNTCP2BlkDateTime:
				{
					LogPrint (eLogDebug, "NTCP2: Datetime");
					if (m_IsEstablished)
					{
						uint64_t ts = i2p::util::GetSecondsSinceEpoch ();
						uint64_t tsA = bufbe32toh (frame + offset);
						if (tsA < ts - NTCP2_CLOCK_SKEW || tsA > ts + NTCP2_CLOCK_SKEW)
						{
							LogPrint (eLogWarning, "NTCP2: Established session time difference ", (int)(ts - tsA), " exceeds clock skew");
							SendTerminationAndTerminate (eNTCP2ClockSkew);
						}
					}
					break;
				}
				case eNTCP2BlkOptions:
					LogPrint (eLogDebug, "NTCP2: Options");
				break;
				case eNTCP2BlkRouterInfo:
				{
					LogPrint (eLogDebug, "NTCP2: RouterInfo flag=", (int)frame[offset]);
					i2p::data::netdb.PostI2NPMsg (CreateI2NPMessage (eI2NPDummyMsg, frame + offset, size));
					break;
				}
				case eNTCP2BlkI2NPMessage:
				{
					LogPrint (eLogDebug, "NTCP2: I2NP");
					if (size > I2NP_MAX_MESSAGE_SIZE)
					{
						LogPrint (eLogError, "NTCP2: I2NP block is too long ", size);
						break;
					}
					auto nextMsg = (frame[offset] == eI2NPTunnelData) ? NewI2NPTunnelMessage (true) : NewI2NPMessage (size);
					nextMsg->len = nextMsg->offset + size + 7; // 7 more bytes for full I2NP header
					if (nextMsg->len <= nextMsg->maxLen)
					{
						memcpy (nextMsg->GetNTCP2Header (), frame + offset, size);
						nextMsg->FromNTCP2 ();
						m_Handler.PutNextMessage (std::move (nextMsg));
					}
					else
						LogPrint (eLogError, "NTCP2: I2NP block is too long for I2NP message");
					break;
				}
				case eNTCP2BlkTermination:
					if (size >= 9)
					{
						LogPrint (eLogDebug, "NTCP2: Termination. reason=", (int)(frame[offset + 8]));
						Terminate ();
					}
					else
						LogPrint (eLogWarning, "NTCP2: Unexpected termination block size ", size);
				break;
				case eNTCP2BlkPadding:
					LogPrint (eLogDebug, "NTCP2: Padding");
				break;
				default:
					LogPrint (eLogWarning, "NTCP2: Unknown block type ", (int)blk);
			}
			offset += size;
		}
		m_Handler.Flush ();
	}
```

该函数用于处理接收到的NTCP2帧。它接收一个指向帧数据的指针 `frame`和帧的长度 `len`作为参数。

函数使用一个循环来遍历帧中的每个块，并根据块的类型执行相应的操作。循环中的 `offset`变量用于跟踪当前处理的位置。

在每次循环迭代中，首先从 `frame`中读取块的类型 `blk`，然后增加 `offset`以读取块的大小 `size`。然后，它输出调试日志，显示块的类型和大小。

接下来，根据块的类型执行相应的操作：

* 如果块类型为 `eNTCP2BlkDateTime`，表示该块包含日期时间信息。如果会话已建立，则获取当前时间戳，并与从块中读取的时间戳进行比较。如果时间戳的差异超过预设的时间范围（`NTCP2_CLOCK_SKEW`），则输出警告日志并发送终止消息以终止会话。
* 如果块类型为 `eNTCP2BlkOptions`，表示该块包含选项信息。输出调试日志。
* 如果块类型为 `eNTCP2BlkRouterInfo`，表示该块包含路由器信息。输出调试日志，并将块的数据传递给 `i2p::data::netdb.PostI2NPMsg`函数进行处理。
* 如果块类型为 `eNTCP2BlkI2NPMessage`，表示该块包含I2NP消息。首先检查块的大小是否超过最大限制（`I2NP_MAX_MESSAGE_SIZE`）。然后，根据块中的数据创建一个新的I2NP消息，并将消息的数据从块中复制到消息中。最后，将消息传递给 `m_Handler`对象进行处理。
* 如果块类型为 `eNTCP2BlkTermination`，表示该块包含终止信息。如果块的大小足够大（至少9个字节），则输出调试日志并调用 `Terminate`函数终止会话。
* 如果块类型为 `eNTCP2BlkPadding`，表示该块是填充块。输出调试日志。
* 如果块类型为其他未知类型，则输出警告日志。

在处理完所有块后，调用 `m_Handler`对象的 `Flush`函数。

#### I2NP数据处理

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

### client端

#### 入口

```
void Transports::SendMessages (const i2p::data::IdentHash& ident, const std::vector<std::shared_ptr<i2p::I2NPMessage> >& msgs)
	{
		m_Service->post (std::bind (&Transports::PostMessages, this, ident, msgs));
	}
```

#### 尝试连接到指定标识符对应的对等体，并根据路由器信息和优先级选择合适的传输方式进行连接

```
bool Transports::ConnectToPeer (const i2p::data::IdentHash& ident, Peer& peer)
	{
```

#### 请求获取指定目的地的路由器信息

请求获取指定目的地的路由器信息。它根据 `direct` 参数的值，选择直接发送请求或通过隧道发送请求

```
	void NetDb::RequestDestination (const IdentHash& destination, RequestedDestination::RequestComplete requestComplete, bool direct)
	{
		auto dest = m_Requests.CreateRequest (destination, false, requestComplete); // non-exploratory
		if (!dest)
		{
			LogPrint (eLogWarning, "NetDb: Destination ", destination.ToBase64(), " is requested already");
			return;
		}

		auto floodfill = GetClosestFloodfill (destination, dest->GetExcludedPeers ());
		if (floodfill)
		{
			if (direct && !floodfill->IsReachableFrom (i2p::context.GetRouterInfo ()) &&
				!i2p::transport::transports.IsConnected (floodfill->GetIdentHash ()))
				direct = false; // floodfill can't be reached directly
			if (direct)
				transports.SendMessage (floodfill->GetIdentHash (), dest->CreateRequestMessage (floodfill->GetIdentHash ()));
			else
			{
				auto pool = i2p::tunnel::tunnels.GetExploratoryPool ();
				auto outbound = pool ? pool->GetNextOutboundTunnel (nullptr, floodfill->GetCompatibleTransports (false)) : nullptr;
				auto inbound = pool ? pool->GetNextInboundTunnel (nullptr, floodfill->GetCompatibleTransports (true)) : nullptr;
				if (outbound &&	inbound)
					outbound->SendTunnelDataMsgTo (floodfill->GetIdentHash (), 0, dest->CreateRequestMessage (floodfill, inbound));
				else
				{
					LogPrint (eLogError, "NetDb: ", destination.ToBase64(), " destination requested, but no tunnels found");
					m_Requests.RequestComplete (destination, nullptr);
				}
			}
		}
		else
		{
			LogPrint (eLogError, "NetDb: ", destination.ToBase64(), " destination requested, but no floodfills found");
			m_Requests.RequestComplete (destination, nullptr);
		}
	}
```


1. 首先调用 `m_Requests.CreateRequest` 函数创建一个新的路由器信息请求，并将其存储在 `dest` 中。如果创建请求失败（例如，已经存在该目的地的请求），则输出警告日志并返回。
2. 接下来，通过调用 `GetClosestFloodfill` 函数获取与目的地最接近的洪泛填充节点，并排除已经请求过的洪泛填充节点。
3. 如果找到了洪泛填充节点，则根据 `direct` 的值决定是直接发送请求还是通过隧道发送请求。具体操作如下：
   * 如果 `direct` 为 `true`，并且洪泛填充节点不可直接访问（不可达或未连接），则将 `direct` 设置为 `false`，表示无法直接访问洪泛填充节点。
   * 如果 `direct` 为 `true`，则调用 `transports.SendMessage` 函数向洪泛填充节点发送请求消息。
   * 如果 `direct` 为 `false`，则从探索隧道池中获取下一个出站隧道和入站隧道，并调用 `outbound->SendTunnelDataMsgTo` 函数将请求消息发送给洪泛填充节点。
   * 如果无法获取到出站隧道或入站隧道，则输出错误日志，并调用 `m_Requests.RequestComplete` 函数触发请求完成的回调，并将目的地标识符和空指针作为参数传递。
4. 如果没有找到洪泛填充节点，则输出错误日志，并调用 `m_Requests.RequestComplete` 函数触发请求完成的回调，并将目的地标识符和空指针作为参数传递。


#### 连接server ,创建会话请求,进行双方的确认后,建立链接

(和client的处理对应)

```
void NTCP2Server::HandleConnect (const boost::system::error_code& ecode, std::shared_ptr<NTCP2Session> conn, std::shared_ptr<boost::asio::deadline_timer> timer)
	{
		timer->cancel ();
		if (ecode)
		{
			LogPrint (eLogInfo, "NTCP2: Connect error ", ecode.message ());
			conn->Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "NTCP2: Connected to ", conn->GetRemoteEndpoint ());
			conn->ClientLogin ();
		}
	}
```

#### 发生I2NPMESG (加上ntcp的头)

```
void SSU2Session::SendI2NPMessages (const std::vector<std::shared_ptr<I2NPMessage> >& msgs)
	{
		m_Server.GetService ().post (std::bind (&SSU2Session::PostI2NPMessages, shared_from_this (), msgs));
	}
```
