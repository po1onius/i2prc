1,初始化,包括

1. 设置存储位置为数据目录。
2. 初始化存储器
3. 初始化配置文件的存储。
4. 加载证书。
5. 清除旧数据，并遍历文件系统，逐个加载路由器信息文件
   (load函数处理,读取所有的信息,单纯靠NETDB的信息进行处理)

2,过读取配置选项 "reseed.floodfill" 的值 若不为空,重新填充种子

```
种子获取的配置文件
reseed.add_options()
			("reseed.verify", value<bool>()->default_value(false),        "Verify .su3 signature")
			("reseed.threshold", value<uint16_t>()->default_value(25),    "Minimum number of known routers before requesting reseed")
			("reseed.floodfill", value<std::string>()->default_value(""), "Path to router info of floodfill to reseed from")
			("reseed.file", value<std::string>()->default_value(""),      "Path to local .su3 file or HTTPS URL to reseed from")
			("reseed.zipfile", value<std::string>()->default_value(""),   "Path to local .zip file to reseed from")
			("reseed.proxy", value<std::string>()->default_value(""),     "url for reseed proxy, supports http/socks")
			("reseed.urls", value<std::string>()->default_value(
				"https://reseed2.i2p.net/,"
				"https://reseed.diva.exchange/,"
				"https://reseed-fr.i2pd.xyz/,"
				"https://reseed.memcpy.io/,"
				"https://reseed.onion.im/,"
				"https://i2pseed.creativecowpat.net:8443/,"
				"https://reseed.i2pgit.org/,"
				"https://banana.incognet.io/,"
				"https://reseed-pl.i2pd.xyz/,"
				"https://www2.mk16.de/,"
			    "https://i2p.ghativega.in/,"
			    "https://i2p.novg.net/"
			),                                                            "Reseed URLs, separated by comma")
			("reseed.yggurls", value<std::string>()->default_value(
				"http://[324:71e:281a:9ed3::ace]:7070/,"
				"http://[301:65b9:c7cd:9a36::1]:18801/,"
				"http://[320:8936:ec1a:31f1::216]/,"
				"http://[306:3834:97b9:a00a::1]/,"
				"http://[316:f9e0:f22e:a74f::216]/"
			),                                                            "Reseed URLs through the Yggdrasil, separated by comma")
		;
```

3.`NetDb::Run`

从`m_Queue`获取msg信息进行处理(由ntcp2,SSU等模块获取)

* `eI2NPDummyMsg = 0`：将路由器信息添加到本地 NetDb 中,并若自身是洪泛节点广播该条信息
* `eI2NPDatabaseStore = 1`：数据库存储消息
* `eI2NPDatabaseLookup = 2`：数据库查找消息
* `eI2NPDatabaseSearchReply = 3`：数据库搜索回复消息

4.Explore(),自动探索新的路由器信息
获取当前节点的探索隧道池中的出站和入站隧道。如果获取成功，说明当前节点可以通过隧道发送和接收信息。通过 `GetClosestFloodfill()` 函数查找距离一个随机哈希最近的洪泛填充节点（floodfill），并将该随机哈希添加为一个新的探索请求。

然后根据情况，通过隧道或直接向洪泛填充节点发送建立连接的请求。

当前节点可以访问出站和入站的隧道，则首先将一个 DBnet存储消 用于通知洪泛节点自身存在，并告知其自己可用的 IP 和端口号等信息。接着，创建一个探索请求的消息，同时将该消息也添加到消息队列中，表示通过隧道发送到洪泛节点。获取信息

数据流转接口

1.创建数据流

```
std::shared_ptr<i2p::stream::Stream> ClientDestination::CreateStream (const i2p::data::IdentHash& dest, uint16_t port)
	{
		return CreateStreamSync (dest, port);
	}
```

2.发送数据

```
size_t Stream::Send (const uint8_t * buf, size_t len)
	{
		AsyncSend (buf, len, nullptr);
		return len;
	}
```

3.接收数据

```
size_t Stream::Receive (uint8_t * buf, size_t len, int timeout)
	{
		if (!len) return 0;
		size_t ret = 0;
		volatile bool done = false;
		std::condition_variable newDataReceived;
		std::mutex newDataReceivedMutex;
		AsyncReceive (boost::asio::buffer (buf, len),
			[&ret, &done, &newDataReceived, &newDataReceivedMutex](const boost::system::error_code& ecode, std::size_t bytes_transferred)
			{
				if (ecode == boost::asio::error::timed_out)
					ret = 0;
				else
					ret = bytes_transferred;
				std::unique_lock<std::mutex> l(newDataReceivedMutex);
				newDataReceived.notify_all ();
				done = true;
			},
			timeout);
		if (!done)
		{	std::unique_lock<std::mutex> l(newDataReceivedMutex);
			if (!done && newDataReceived.wait_for (l, std::chrono::seconds (timeout)) == std::cv_status::timeout)
				ret = 0;
		}
		if (!done)
		{
			// make sure that AsycReceive complete
			auto s = shared_from_this();
			m_Service.post ([s]()
		    {
				s->m_ReceiveTimer.cancel ();
			});
			int i = 0;
			while (!done && i < 100) // 1 sec
			{
				std::this_thread::sleep_for (std::chrono::milliseconds(10));
				i++;
			}
		}
		return ret;
	}
```

4.关闭链接

```
void DestroyStream (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream)
			stream->Close ();
	}
```

5.接受链接

```
std::shared_ptr<Stream> StreamingDestination::AcceptStream (int timeout)
	{
		std::shared_ptr<i2p::stream::Stream> stream;
		std::condition_variable streamAccept;
		std::mutex streamAcceptMutex;
		std::unique_lock<std::mutex> l(streamAcceptMutex);
		AcceptOnce (
			[&streamAccept, &streamAcceptMutex, &stream](std::shared_ptr<i2p::stream::Stream> s)
		    {
				stream = s;
				std::unique_lock<std::mutex> l(streamAcceptMutex);
				streamAccept.notify_all ();
			});
		if (timeout)
			streamAccept.wait_for (l, std::chrono::seconds (timeout));
		else
			streamAccept.wait (l);
		return stream;
	}
```

5.监听链接

