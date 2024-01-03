### 封装流I2NP头

```
struct stream_i2np
{
	uint8_t * i2np_header;

	uint32_t payload_size;//压缩或者为压缩的长度
	uint8_t * payload;//压缩或者不压缩
	uint16_t src_port;
	uint16_t dst_port;
	uint8_t protocol;// streaming protocol
}
```

### 流负载的数据结构

```
	struct stream_packet
	{
	 	u_int32_t sendStreamID;
		u_int32_t receiveStreamID;
		u_int32_t sequenceNumber;
		u_int32_t ack;//((uint32_t)ACK记录 非首包:最后一个接收到的数据包的序列号 首包:0,)
		u_int8_t syn;//((uint8_t)SYN记录是否是首包 0:非首包 8:首包)
		if (m_Status == eStreamStatusNew)
		//，如果数据流是新创建的且发送方还没有发送过数据流ID且已经获取到了远程节点的身份信息，
		//那么就需要发送第一个SYN数据包来初始化连接
		{
			u_int8_t *remotehash;//(32)
		}
		  u_int8_t m_RTO/1000;//(毫秒)(重传时间)
		  u_int16_t flags;

		  u_int16_t optionsSize;//包括localhsah+m_MTU+offlineSignature+signature
		  u_int8_t *Localhash;
		  u_int16_t m_MTU;
		if(支持离线签名)
		{
			u_int8_t *offlineSignature;
		}
		  u_int8_t *signature;//签名字段
		  u_int8_t *payload;//根据最大m_MTU进行分片
	}(非新流未写)
```

### 1.端口号

根据I2P官方网站的描述，I2P在运行时会从大于1 024的端口号中随机选取一个端口号进行通信。

```c
libi2pd/RouterContext.cpp
uint16_t RouterContext::SelectRandomPort () const
	{
		uint16_t port;
		do
		{
			port = rand () % (30777 - 9111) + 9111; // I2P network ports range
		}
		while(i2p::util::net::IsPortInReservedRange(port));

		return port;
	}
```

### 2.修改单次包的最大传输单元

`m_MTU = m_RoutingSession->IsRatchets () ? STREAMING_MTU_RATCHETS : STREAMING_MTU;`

```
libi2pd/Streaming.h
const size_t STREAMING_MTU = 1730;
	const size_t STREAMING_MTU_RATCHETS = 1812;
```

### 3.tunnel隧道建立过程

添加一个混淆hash

```
libi2pd/Tunnel.h
	const int STANDARD_NUM_RECORDS = 4; // in VariableTunnelBuild message
	const int MAX_NUM_RECORDS = 8;
```
