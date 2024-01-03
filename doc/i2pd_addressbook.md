# addressbook(控制I2P地址簿)

server端主要用于数据的接受以及处理将收到的数据包根据标识进行查表解析,将hash更新到m_address容器中或写入文件

其中数据包的处理回调是根据数据包负载中目的端口作为键值进行的

## server启动过程

daemon/Daemon.cpp

`bool Daemon_Singleton::start()`->`i2p::client::context.Start ();`->`m_AddressBook.Start ();`

## 从存储器或者文件中加载地址簿中的主机信息

```
void AddressBook::LoadHosts()
{
    if (m_Storage->Load(m_Addresses) > 0) {
        m_IsLoaded = true;
        return;
    }

    // then try hosts.txt
    std::ifstream f(i2p::fs::DataDirPath("hosts.txt"),
                    std::ifstream::in);  // in text mode
    if (f.is_open()) {
        LoadHostsFromStream(f, false);
        m_IsLoaded = true;
    }

    // reset eTags, because we don’t know how old hosts.txt is or can't load addressbook
    m_Storage->ResetEtags();
}
```

## 启动订阅功能

代码通过 `i2p::client::context.GetSharedLocalDestination()`获取共享本地目标（destination）。如果成功获取到共享本地目标，则执行以下操作：

1. 创建一个 `boost::asio::deadline_timer`对象，并将其指针赋值给 `m_SubscriptionsUpdateTimer`成员变量。
2. 使用 `expires_from_now()`函数设置定时器的超时时间为 `INITIAL_SUBSCRIPTION_UPDATE_TIMEOUT`分钟后。
3. 使用 `async_wait()`函数异步等待定时器超时，并指定回调函数为 `AddressBook::HandleSubscriptionsUpdateTimer()`，同时绑定了当前对象的指针作为参数。

## 用于启动地址查找功能

首先，代码通过 `i2p::client::context.GetSharedLocalDestination()`获取共享本地目标（destination）。如果成功获取到共享本地目标，则执行以下操作：

1. 使用 `GetDatagramDestination()`函数获取目标的数据报目标（datagram destination）。
2. 如果数据报目标不存在，则调用 `CreateDatagramDestination()`函数创建一个新的数据报目标。
3. 使用 `SetReceiver()`函数设置数据报目标的接收器，指定回调函数为 `AddressBook::HandleLookupResponse()`，同时绑定了当前对象的指针以及其他参数作为回调函数的参数。

总之，`StartLookups()`函数的作用是获取共享本地目标，并为其设置数据报目标的接收器。如果数据报目标不存在，则创建一个新的数据报目标。这样，地址查找功能就可以在接收到相应的回复时调用 `HandleLookupResponse()`函数进行处理。

(拿到的数据都是通过tunnel模块解密处理后的明文)

### 数据包接收(关键点)`  datagram->SetReceiver`端口54

代码中使用了 `std::lock_guard<std::mutex>`来创建一个互斥锁（mutex）的保护范围，确保在多线程环境下对接收器进行安全的访问。

函数接受两个参数：

1. `receiver`是一个函数对象，表示回调函数。该函数对象可以是普通函数、成员函数、Lambda表达式等，用于处理接收到的数据。
2. `port`是一个无符号16位整数，表示接收器绑定的端口号。

在函数内部，首先通过 `std::lock_guard<std::mutex>`创建了一个互斥锁的保护范围，确保在设置接收器时的线程安全性。

然后，将接收器 `receiver`与端口号 `port`存储到 `m_ReceiversByPorts`容器中,以端口号为键，将接收器与端口号关联起来，以便在接收到数据时能够根据端口号找到相应的接收器进行处理。

`void SetReceiver (const Receiver& receiver, uint16_t port) { std::lock_guardstd::mutex lock(m_ReceiversMutex); m_ReceiversByPorts[port] = receiver; };`

```
 datagram->SetReceiver(
            std::bind(&AddressBook::HandleLookupResponse, this,
                      std::placeholders::_1, std::placeholders::_2,
                      std::placeholders::_3, std::placeholders::_4,
                      std::placeholders::_5),
            ADDRESS_RESPONSE_DATAGRAM_PORT);
```

### 数据包处理 `HandleLookupResponse`

1. 首先，通过比较 `len`和44的大小，判断接收到的数据长度是否小于44。如果小于44，则打印错误日志并返回，因为这个长度是一个有效响应的最小长度要求。
2. 如果数据长度合法，那么从接收到的数据中提取出一个32位的 `nonce`（在buf + 4的位置），用于标识这次查找的唯一性。
3. 打印调试日志，显示接收到了来自 `from.GetIdentHash().ToBase32()`的查找响应，以及对应的 `nonce`值。
4. 在获取地址之前，首先获取了 `m_LookupsMutex`的独占锁，确保在查找过程中的线程安全性。
5. 在 `m_Lookups`容器中查找具有相同 `nonce`的元素。如果找到了对应的元素，将其关联的地址存储到 `address`变量中，并从 `m_Lookups`容器中删除该元素。
6. 如果 `address`的长度大于0，说明找到了对应的地址。接下来，通过从 `buf + 8`的位置提取出一个 `IdentHash`，创建一个 `Address`对象，并将其与 `address`关联起来，存储到 `m_Addresses`容器中。
7. 如果提取的 `IdentHash`是全零的，说明找不到对应的地址。在这种情况下，打印信息日志，表示查找响应中的地址不存在。

总之，这段代码的作用是处理接收到的地址查找响应。它首先检查数据长度是否合法，然后提取出 `nonce`和地址，并将地址与对应的 `IdentHash`关联起来存储到 `m_Addresses`容器中。如果地址不存在，则输出相应的信息日志。

## clinet发送端

## 启动

启动地址解析器，并加载本地地址信息

```
void AddressBook::StartResolvers()
{
    LoadLocal();
}
```

在函数中，首先判断存储对象 `m_Storage`是否存在，如果不存在，则直接返回。接着，定义了一个名为localAddresses的变量，用于保存本地地址信息。然后，调用了存储对象的成员函数LoadLocal()，将本地地址信息加载到localAddresses中。

接下来，函数遍历localAddresses中的所有地址信息。如果地址信息不是标识哈希，则跳过不处理。如果地址信息包含域名，则从地址簿m_Addresses中查找对应的域名。如果找到了对应的域名，并且该域名对应的标识哈希也是本地的，则创建或获取对应的地址解析器，并将该地址信息添加到解析器中。

最后，函数结束。总之，这段代码的功能是加载本地地址信息，并将其添加到地址解析器中。

## 数据包发送接口(回调HandleRequst)

和接受一致只是绑定的端口不一样,端口54

处理地址解析请求，根据请求的地址，在本地地址列表中查找对应的标识哈希，并将响应发送给请求方。

```
void AddressResolver::HandleRequest(const i2p::data::IdentityEx &from,
                                    uint16_t fromPort, uint16_t toPort,
                                    const uint8_t *buf, size_t len)
{
    if (len < 9 || len < buf[8] + 9U) {
        LogPrint(eLogError, "Addressbook: Address request is too short ", len);
        return;
    }
    // read requested address
    uint8_t l = buf[8];
    char address[255];
    memcpy(address, buf + 9, l);
    address[l] = 0;
    LogPrint(eLogDebug, "Addressbook: Address request ", address);
    // send response
    uint8_t response[44];
    memset(response, 0, 4);                    // reserved
    memcpy(response + 4, buf + 4, 4);          // nonce
    auto it = m_LocalAddresses.find(address);  // address lookup
    if (it != m_LocalAddresses.end())
        memcpy(response + 8, it->second, 32);  // ident
    else
        memset(response + 8, 0, 32);  // not found
    memset(response + 40, 0, 4);      // set expiration time to zero
    m_LocalDestination->GetDatagramDestination()->SendDatagramTo(
        response, 44, from.GetIdentHash(), toPort, fromPort);
}
```

### 发送数据

response：要发送的数据包内容，可能是一个字符串或者字节数组等。

1. 44：数据包内容的长度，单位可能是字节。
2. from.GetIdentHash()：数据包的源地址，可能是一个哈希值。
3. toPort：数据包要发送到的目标端口号。
4. fromPort：数据包从哪个端口号发送出去的。

```
 m_LocalDestination->GetDatagramDestination()->SendDatagramTo(
        response, 44, from.GetIdentHash(), toPort, fromPort);
```


1. 初始化一个空指针类型的 `session`。
2. 使用互斥锁 `m_SessionsMutex`进行线程安全保护。
3. 在 `m_Sessions`中查找与给定身份标识匹配的会话对象。
4. 如果找不到匹配的会话对象，则创建一个新的 `DatagramSession`对象，并将其加入到 `m_Sessions`中。
5. 如果找到了匹配的会话对象，则将 `session`指向该对象。
6. 返回 `session`。

根据代码的逻辑和命名，可以推测 `ObtainSession()`方法的作用是获取与给定身份标识相关的会话对象。如果找不到匹配的会话对象，则创建一个新的会话对象并返回。这个方法可能是用于管理和复用会话对象，以提高数据包发送的效率和性能。

需要注意的是，这段代码使用了互斥锁来保护对 `m_Sessions`的访问，

```
	std::shared_ptr<DatagramSession> DatagramDestination::ObtainSession(const i2p::data::IdentHash & identity)
	{
		std::shared_ptr<DatagramSession> session = nullptr;
		std::lock_guard<std::mutex> lock(m_SessionsMutex);
		auto itr = m_Sessions.find(identity);
		if (itr == m_Sessions.end()) {
			// not found, create new session
			session = std::make_shared<DatagramSession>(m_Owner, identity);
			session->Start ();
			m_Sessions[identity] = session;
		} else {
			session = itr->second;
		}
		return session;
	}

```
