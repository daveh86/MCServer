
// Network.cpp

// Implements the classes used for the Network API

#include "Globals.h"
#include "Network.h"
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/bufferevent.h>
#include <event2/dns.h>
#include <event2/listener.h>
#include <thread>
#include "Event.h"
#include "CriticalSection.h"
#include "NetworkSingleton.h"





// fwd:
class cServerHandleImpl;
class cTCPLinkImpl;
typedef SharedPtr<cTCPLinkImpl> cTCPLinkImplPtr;
typedef std::vector<cTCPLinkImplPtr> cTCPLinkImplPtrs;
typedef SharedPtr<cServerHandleImpl> cServerHandleImplPtr;
typedef std::vector<cServerHandleImplPtr> cServerHandleImplPtrs;





////////////////////////////////////////////////////////////////////////////////
// Class definitions:

/** Holds information about an in-progress Hostname-to-IP lookup. */
class cHostnameLookup
{
	/** The callbacks to call for resolved names / errors. */
	cNetwork::cResolveNameCallbacksPtr m_Callbacks;

	/** The hostname that was queried (needed for the callbacks). */
	AString m_Hostname;

	static void Callback(int a_ErrCode, struct evutil_addrinfo * a_Addr, void * a_Self);

public:
	cHostnameLookup(const AString & a_Hostname, cNetwork::cResolveNameCallbacksPtr a_Callbacks);
};
typedef SharedPtr<cHostnameLookup> cHostnameLookupPtr;
typedef std::vector<cHostnameLookupPtr> cHostnameLookupPtrs;





/** Holds information about an in-progress IP-to-Hostname lookup. */
class cIPLookup
{
	/** The callbacks to call for resolved names / errors. */
	cNetwork::cResolveNameCallbacksPtr m_Callbacks;

	/** The IP that was queried (needed for the callbacks). */
	AString m_IP;

	static void Callback(int a_Result, char a_Type, int a_Count, int a_Ttl, void * a_Addresses, void * a_Self);

public:
	cIPLookup(const AString & a_IP, cNetwork::cResolveNameCallbacksPtr a_Callbacks);
};
typedef SharedPtr<cIPLookup> cIPLookupPtr;
typedef std::vector<cIPLookupPtr> cIPLookupPtrs;





/** Implements the cTCPLink details so that it can represent the single connection between two endpoints. */
class cTCPLinkImpl:
	public cTCPLink
{
	typedef cTCPLink super;

public:
	/** Creates a new link based on the given socket.
	Used for connections accepted in a server using cNetwork::Listen().
	a_Address and a_AddrLen describe the remote peer that has connected. */
	cTCPLinkImpl(evutil_socket_t a_Socket, cCallbacksPtr a_LinkCallbacks, cServerHandleImpl * a_Server, const sockaddr * a_Address, int a_AddrLen);

	/** Destroys the LibEvent handle representing the link. */
	~cTCPLinkImpl();

	/** Queues a connection request to the specified host.
	a_ConnectCallbacks must be valid.
	Returns a link that has the connection request queued, or NULL for failure. */
	static cTCPLinkImplPtr Connect(const AString & a_Host, UInt16 a_Port, cTCPLink::cCallbacksPtr a_LinkCallbacks, cNetwork::cConnectCallbacksPtr a_ConnectCallbacks);

	// cTCPLink overrides:
	virtual bool Send(const void * a_Data, size_t a_Length) override;
	virtual AString GetLocalIP(void) const override { return m_LocalIP; }
	virtual UInt16 GetLocalPort(void) const override { return m_LocalPort; }
	virtual AString GetRemoteIP(void) const override { return m_RemoteIP; }
	virtual UInt16 GetRemotePort(void) const override { return m_RemotePort; }
	virtual void Shutdown(void) override;
	virtual void Close(void) override;

protected:

	/** Callbacks to call when the connection is established.
	May be NULL if not used. Only used for outgoing connections (cNetwork::Connect()). */
	cNetwork::cConnectCallbacksPtr m_ConnectCallbacks;

	/** The LibEvent handle representing this connection. */
	bufferevent * m_BufferEvent;

	/** The server handle that has created this link.
	Only valid for incoming connections, NULL for outgoing connections. */
	cServerHandleImpl * m_Server;

	/** The IP address of the local endpoint. Valid only after the socket has been connected. */
	AString m_LocalIP;

	/** The port of the local endpoint. Valid only after the socket has been connected. */
	UInt16 m_LocalPort;

	/** The IP address of the remote endpoint. Valid only after the socket has been connected. */
	AString m_RemoteIP;

	/** The port of the remote endpoint. Valid only after the socket has been connected. */
	UInt16 m_RemotePort;


	/** Creates a new link to be queued to connect to a specified host:port.
	Used for outgoing connections created using cNetwork::Connect().
	To be used only by the Connect() factory function. */
	cTCPLinkImpl(const cCallbacksPtr a_LinkCallbacks);

	/** Callback that LibEvent calls when there's data available from the remote peer. */
	static void ReadCallback(bufferevent * a_BufferEvent, void * a_Self);

	/** Callback that LibEvent calls when there's a non-data-related event on the socket. */
	static void EventCallback(bufferevent * a_BufferEvent, short a_What, void * a_Self);

	/** Sets a_IP and a_Port to values read from a_Address, based on the correct address family. */
	static void UpdateAddress(const sockaddr * a_Address, int a_AddrLen, AString & a_IP, UInt16 & a_Port);

	/** Updates m_LocalIP and m_LocalPort based on the metadata read from the socket. */
	void UpdateLocalAddress(void);

	/** Updates m_RemoteIP and m_RemotePort based on the metadata read from the socket. */
	void UpdateRemoteAddress(void);
};





/** Implements the cServerHandle details so that it can represent a real server socket, with a list of clients. */
class cServerHandleImpl:
	public cServerHandle
{
	typedef cServerHandle super;
	friend class cTCPLinkImpl;

public:
	/** Closes the server, dropping all the connections. */
	~cServerHandleImpl();

	/** Creates a new server instance listening on the specified port.
	Both IPv4 and IPv6 interfaces are used, if possible.
	Always returns a server instance; in the event of a failure, the instance holds the error details. Use IsListening() to query success. */
	static cServerHandleImplPtr Listen(
		UInt16 a_Port,
		cNetwork::cListenCallbacksPtr a_ListenCallbacks,
		cTCPLink::cCallbacksPtr a_LinkCallbacks
	);

	// cServerHandle overrides:
	virtual void Close(void) override;
	virtual bool IsListening(void) const override { return m_IsListening; }

protected:
	/** The callbacks used to notify about incoming connections. */
	cNetwork::cListenCallbacksPtr m_ListenCallbacks;

	/** The callbacks used to create new cTCPLink instances for incoming connections. */
	cTCPLink::cCallbacksPtr m_LinkCallbacks;

	/** The LibEvent handle representing the main listening socket. */
	evconnlistener * m_ConnListener;

	/** The LibEvent handle representing the secondary listening socket (only when side-by-side listening is needed, such as WinXP). */
	evconnlistener * m_SecondaryConnListener;

	/** Set to true when the server is initialized successfully and is listening for incoming connections. */
	bool m_IsListening;

	/** Container for all currently active connections on this server. */
	cTCPLinkImplPtrs m_Connections;

	/** Mutex protecting m_Connections againt multithreaded access. */
	cCriticalSection m_CS;

	/** Contains the error code for the failure to listen. Only valid for non-listening instances. */
	int m_ErrorCode;

	/** Contains the error message for the failure to listen. Only valid for non-listening instances. */
	AString m_ErrorMsg;



	/** Creates a new instance with the specified callbacks.
	Initializes the internals, but doesn't start listening yet. */
	cServerHandleImpl(
		cNetwork::cListenCallbacksPtr a_ListenCallbacks,
		cTCPLink::cCallbacksPtr a_LinkCallbacks
	);

	/** Starts listening on the specified port.
	Returns true if successful, false on failure. On failure, sets m_ErrorCode and m_ErrorMsg. */
	bool Listen(UInt16 a_Port);

	/** The callback called by LibEvent upon incoming connection. */
	static void Callback(evconnlistener * a_Listener, evutil_socket_t a_Socket, sockaddr * a_Addr, int a_Len, void * a_Self);

	/** Removes the specified link from m_Connections.
	Called by cTCPLinkImpl when the link is terminated. */
	void RemoveLink(const cTCPLinkImpl * a_Link);
};





////////////////////////////////////////////////////////////////////////////////
// Globals:

bool IsValidSocket(evutil_socket_t a_Socket)
{
	#ifdef _WIN32
		return (a_Socket != INVALID_SOCKET);
	#else  // _WIN32
		return (a_Socket >= 0);
	#endif  // else _WIN32
}





////////////////////////////////////////////////////////////////////////////////
// cHostnameLookup:

cHostnameLookup::cHostnameLookup(const AString & a_Hostname, cNetwork::cResolveNameCallbacksPtr a_Callbacks):
	m_Callbacks(a_Callbacks),
	m_Hostname(a_Hostname)
{
	evutil_addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = EVUTIL_AI_CANONNAME;
	evdns_getaddrinfo(cNetworkSingleton::Get().GetDNSBase(), a_Hostname.c_str(), nullptr, &hints, Callback, this);
}





void cHostnameLookup::Callback(int a_ErrCode, evutil_addrinfo * a_Addr, void * a_Self)
{
	// Get the Self class:
	cHostnameLookup * Self = reinterpret_cast<cHostnameLookup *>(a_Self);
	ASSERT(Self != nullptr);

	// If an error has occurred, notify the error callback:
	if (a_ErrCode != 0)
	{
		Self->m_Callbacks->OnError(a_ErrCode);
		cNetworkSingleton::Get().RemoveHostnameLookup(Self);
		return;
	}

	// Call the success handler for each entry received:
	bool HasResolved = false;
	evutil_addrinfo * OrigAddr = a_Addr;
	for (;a_Addr != nullptr; a_Addr = a_Addr->ai_next)
	{
		char IP[128];
		switch (a_Addr->ai_family)
		{
			case AF_INET:  // IPv4
			{
				sockaddr_in * sin = reinterpret_cast<sockaddr_in *>(a_Addr->ai_addr);
				evutil_inet_ntop(AF_INET, &(sin->sin_addr), IP, sizeof(IP));
				break;
			}
			case AF_INET6:  // IPv6
			{
				sockaddr_in6 * sin = reinterpret_cast<sockaddr_in6 *>(a_Addr->ai_addr);
				evutil_inet_ntop(AF_INET6, &(sin->sin6_addr), IP, sizeof(IP));
				break;
			}
			default:
			{
				// Unknown address family, handle as if this entry wasn't received
				continue;  // for (a_Addr)
			}
		}
		Self->m_Callbacks->OnNameResolved(Self->m_Hostname, IP);
		HasResolved = true;
	}  // for (a_Addr)

	// If only unsupported families were reported, call the Error handler:
	if (!HasResolved)
	{
		Self->m_Callbacks->OnError(1);
	}
	else
	{
		Self->m_Callbacks->OnFinished();
	}
	evutil_freeaddrinfo(OrigAddr);
	cNetworkSingleton::Get().RemoveHostnameLookup(Self);
}





////////////////////////////////////////////////////////////////////////////////
// cIPLookup:

cIPLookup::cIPLookup(const AString & a_IP, cNetwork::cResolveNameCallbacksPtr a_Callbacks):
	m_Callbacks(a_Callbacks),
	m_IP(a_IP)
{
	sockaddr_storage sa;
	int salen = static_cast<int>(sizeof(sa));
	evutil_parse_sockaddr_port(a_IP.c_str(), reinterpret_cast<sockaddr *>(&sa), &salen);
	switch (sa.ss_family)
	{
		case AF_INET:
		{
			sockaddr_in * sa4 = reinterpret_cast<sockaddr_in *>(&sa);
			evdns_base_resolve_reverse(cNetworkSingleton::Get().GetDNSBase(), &(sa4->sin_addr), 0, Callback, this);
			break;
		}
		case AF_INET6:
		{
			sockaddr_in6 * sa6 = reinterpret_cast<sockaddr_in6 *>(&sa);
			evdns_base_resolve_reverse_ipv6(cNetworkSingleton::Get().GetDNSBase(), &(sa6->sin6_addr), 0, Callback, this);
			break;
		}
		default:
		{
			LOGWARNING("%s: Unknown address family: %d", __FUNCTION__, sa.ss_family);
			ASSERT(!"Unknown address family");
			break;
		}
	}  // switch (address family)
}





void cIPLookup::Callback(int a_Result, char a_Type, int a_Count, int a_Ttl, void * a_Addresses, void * a_Self)
{
	// Get the Self class:
	cIPLookup * Self = reinterpret_cast<cIPLookup *>(a_Self);
	ASSERT(Self != nullptr);

	// Call the proper callback based on the event received:
	if ((a_Result != 0) || (a_Addresses == nullptr))
	{
		// An error has occurred, notify the error callback:
		Self->m_Callbacks->OnError(a_Result);
	}
	else
	{
		// Call the success handler:
		Self->m_Callbacks->OnNameResolved(*(reinterpret_cast<char **>(a_Addresses)), Self->m_IP);
		Self->m_Callbacks->OnFinished();
	}
	cNetworkSingleton::Get().RemoveIPLookup(Self);
}





////////////////////////////////////////////////////////////////////////////////
// cTCPLinkImpl:

cTCPLinkImpl::cTCPLinkImpl(cTCPLink::cCallbacksPtr a_LinkCallbacks):
	super(a_LinkCallbacks),
	m_BufferEvent(bufferevent_socket_new(cNetworkSingleton::Get().GetEventBase(), -1, BEV_OPT_CLOSE_ON_FREE)),
	m_Server(nullptr)
{
	// Create the LibEvent handle, but don't assign a socket to it yet (will be assigned within Connect() method):
	bufferevent_setcb(m_BufferEvent, ReadCallback, nullptr, EventCallback, this);
	bufferevent_enable(m_BufferEvent, EV_READ | EV_WRITE);
}





cTCPLinkImpl::cTCPLinkImpl(evutil_socket_t a_Socket, cTCPLink::cCallbacksPtr a_LinkCallbacks, cServerHandleImpl * a_Server, const sockaddr * a_Address, int a_AddrLen):
	super(a_LinkCallbacks),
	m_BufferEvent(bufferevent_socket_new(cNetworkSingleton::Get().GetEventBase(), a_Socket, BEV_OPT_CLOSE_ON_FREE)),
	m_Server(a_Server)
{
	// Update the endpoint addresses:
	UpdateLocalAddress();
	UpdateAddress(a_Address, a_AddrLen, m_RemoteIP, m_RemotePort);

	// Create the LibEvent handle:
	bufferevent_setcb(m_BufferEvent, ReadCallback, nullptr, EventCallback, this);
	bufferevent_enable(m_BufferEvent, EV_READ | EV_WRITE);
}





cTCPLinkImpl::~cTCPLinkImpl()
{
	bufferevent_free(m_BufferEvent);
}





cTCPLinkImplPtr cTCPLinkImpl::Connect(const AString & a_Host, UInt16 a_Port, cTCPLink::cCallbacksPtr a_LinkCallbacks, cNetwork::cConnectCallbacksPtr a_ConnectCallbacks)
{
	ASSERT(a_LinkCallbacks != nullptr);
	ASSERT(a_ConnectCallbacks != nullptr);

	// Create a new link:
	cTCPLinkImplPtr res{new cTCPLinkImpl(a_LinkCallbacks)};  // Cannot use std::make_shared here, constructor is not accessible
	res->m_ConnectCallbacks = a_ConnectCallbacks;
	cNetworkSingleton::Get().AddLink(res);

	// If a_Host is an IP address, schedule a connection immediately:
	sockaddr_storage sa;
	int salen = static_cast<int>(sizeof(sa));
	if (evutil_parse_sockaddr_port(a_Host.c_str(), reinterpret_cast<sockaddr *>(&sa), &salen) == 0)
	{
		// Insert the correct port:
		if (sa.ss_family == AF_INET6)
		{
			reinterpret_cast<sockaddr_in6 *>(&sa)->sin6_port = htons(a_Port);
		}
		else
		{
			reinterpret_cast<sockaddr_in *>(&sa)->sin_port = htons(a_Port);
		}

		// Queue the connect request:
		if (bufferevent_socket_connect(res->m_BufferEvent, reinterpret_cast<sockaddr *>(&sa), salen) == 0)
		{
			// Success
			return res;
		}
		// Failure
		cNetworkSingleton::Get().RemoveLink(res.get());
		return nullptr;
	}

	// a_Host is a hostname, connect after a lookup:
	if (bufferevent_socket_connect_hostname(res->m_BufferEvent, cNetworkSingleton::Get().GetDNSBase(), AF_UNSPEC, a_Host.c_str(), a_Port) == 0)
	{
		// Success
		return res;
	}
	// Failure
	cNetworkSingleton::Get().RemoveLink(res.get());
	return nullptr;
}





bool cTCPLinkImpl::Send(const void * a_Data, size_t a_Length)
{
	return (bufferevent_write(m_BufferEvent, a_Data, a_Length) == 0);
}





void cTCPLinkImpl::Shutdown(void)
{
	#ifdef _WIN32
		shutdown(bufferevent_getfd(m_BufferEvent), SD_SEND);
	#else
		shutdown(bufferevent_getfd(m_BufferEvent), SHUT_WR);
	#endif
	bufferevent_disable(m_BufferEvent, EV_WRITE);
}





void cTCPLinkImpl::Close(void)
{
	// Disable all events on the socket, but keep it alive:
	bufferevent_disable(m_BufferEvent, EV_READ | EV_WRITE);
	if (m_Server == nullptr)
	{
		cNetworkSingleton::Get().RemoveLink(this);
	}
	else
	{
		m_Server->RemoveLink(this);
	}
}






void cTCPLinkImpl::ReadCallback(bufferevent * a_BufferEvent, void * a_Self)
{
	ASSERT(a_Self != nullptr);
	cTCPLinkImpl * Self = static_cast<cTCPLinkImpl *>(a_Self);
	ASSERT(Self->m_Callbacks != nullptr);

	// Read all the incoming data, in 1024-byte chunks:
	char data[1024];
	size_t length;
	while ((length = bufferevent_read(a_BufferEvent, data, sizeof(data))) > 0)
	{
		Self->m_Callbacks->OnReceivedData(*Self, data, length);
	}
}





void cTCPLinkImpl::EventCallback(bufferevent * a_BufferEvent, short a_What, void * a_Self)
{
	ASSERT(a_Self != nullptr);
	cTCPLinkImpl * Self = static_cast<cTCPLinkImpl *>(a_Self);

	// If an error is reported, call the error callback:
	if (a_What & BEV_EVENT_ERROR)
	{
		// Choose the proper callback to call based on whether we were waiting for connection or not:
		if (Self->m_ConnectCallbacks != nullptr)
		{
			Self->m_ConnectCallbacks->OnError(EVUTIL_SOCKET_ERROR());
		}
		else
		{
			Self->m_Callbacks->OnError(*Self, EVUTIL_SOCKET_ERROR());
			if (Self->m_Server == nullptr)
			{
				cNetworkSingleton::Get().RemoveLink(Self);
			}
			else
			{
				Self->m_Server->RemoveLink(Self);
			}
		}
		return;
	}

	// Pending connection succeeded, call the connection callback:
	if (a_What & BEV_EVENT_CONNECTED)
	{
		if (Self->m_ConnectCallbacks != nullptr)
		{
			Self->m_ConnectCallbacks->OnSuccess(*Self);
			// Reset the connect callbacks so that later errors get reported through the link callbacks:
			Self->m_ConnectCallbacks.reset();
			return;
		}
		Self->UpdateLocalAddress();
		Self->UpdateRemoteAddress();
	}

	// If the connection has been closed, call the link callback and remove the connection:
	if (a_What & BEV_EVENT_EOF)
	{
		Self->m_Callbacks->OnRemoteClosed(*Self);
		if (Self->m_Server != nullptr)
		{
			Self->m_Server->RemoveLink(Self);
		}
		else
		{
			cNetworkSingleton::Get().RemoveLink(Self);
		}
		return;
	}
	
	// Unknown event, report it:
	LOGWARNING("cTCPLinkImpl: Unhandled LibEvent event %d (0x%x)", a_What, a_What);
	ASSERT(!"cTCPLinkImpl: Unhandled LibEvent event");
}





void cTCPLinkImpl::UpdateAddress(const sockaddr * a_Address, int a_AddrLen, AString & a_IP, UInt16 & a_Port)
{
	// Based on the family specified in the address, use the correct datastructure to convert to IP string:
	char IP[128];
	switch (a_Address->sa_family)
	{
		case AF_INET:  // IPv4:
		{
			const sockaddr_in * sin = reinterpret_cast<const sockaddr_in *>(a_Address);
			evutil_inet_ntop(AF_INET, &(sin->sin_addr), IP, sizeof(IP));
			a_Port = ntohs(sin->sin_port);
			break;
		}
		case AF_INET6:  // IPv6
		{
			const sockaddr_in6 * sin = reinterpret_cast<const sockaddr_in6 *>(a_Address);
			evutil_inet_ntop(AF_INET6, &(sin->sin6_addr), IP, sizeof(IP));
			a_Port = ntohs(sin->sin6_port);
			break;
		}

		default:
		{
			LOGWARNING("%s: Unknown socket address family: %d", __FUNCTION__, a_Address->sa_family);
			ASSERT(!"Unknown socket address family");
			break;
		}
	}
	a_IP.assign(IP);
}





void cTCPLinkImpl::UpdateLocalAddress(void)
{
	sockaddr_storage sa;
	socklen_t salen = static_cast<socklen_t>(sizeof(sa));
	getsockname(bufferevent_getfd(m_BufferEvent), reinterpret_cast<sockaddr *>(&sa), &salen);
	UpdateAddress(reinterpret_cast<const sockaddr *>(&sa), salen, m_LocalIP, m_LocalPort);
}





void cTCPLinkImpl::UpdateRemoteAddress(void)
{
	sockaddr_storage sa;
	socklen_t salen = static_cast<socklen_t>(sizeof(sa));
	getpeername(bufferevent_getfd(m_BufferEvent), reinterpret_cast<sockaddr *>(&sa), &salen);
	UpdateAddress(reinterpret_cast<const sockaddr *>(&sa), salen, m_RemoteIP, m_RemotePort);
}





////////////////////////////////////////////////////////////////////////////////
// cServerHandleImpl:

cServerHandleImpl::cServerHandleImpl(
	cNetwork::cListenCallbacksPtr a_ListenCallbacks,
	cTCPLink::cCallbacksPtr a_LinkCallbacks
):
	m_ListenCallbacks(a_ListenCallbacks),
	m_LinkCallbacks(a_LinkCallbacks),
	m_ConnListener(nullptr),
	m_SecondaryConnListener(nullptr),
	m_IsListening(false),
	m_ErrorCode(0)
{
}





cServerHandleImpl::~cServerHandleImpl()
{
	if (m_ConnListener != nullptr)
	{
		evconnlistener_free(m_ConnListener);
	}
	if (m_SecondaryConnListener != nullptr)
	{
		evconnlistener_free(m_SecondaryConnListener);
	}
}





void cServerHandleImpl::Close(void)
{
	// Stop the listener sockets:
	evconnlistener_disable(m_ConnListener);
	if (m_SecondaryConnListener != nullptr)
	{
		evconnlistener_disable(m_SecondaryConnListener);
	}
	m_IsListening = false;

	// Shutdown all connections:
	cTCPLinkImplPtrs Conns;
	{
		cCSLock Lock(m_CS);
		std::swap(Conns, m_Connections);
	}
	for (auto conn: Conns)
	{
		conn->Shutdown();
	}
}





cServerHandleImplPtr cServerHandleImpl::Listen(
	UInt16 a_Port,
	cNetwork::cListenCallbacksPtr a_ListenCallbacks,
	cTCPLink::cCallbacksPtr a_LinkCallbacks
)
{
	cServerHandleImplPtr res = cServerHandleImplPtr{new cServerHandleImpl(a_ListenCallbacks, a_LinkCallbacks)};
	if (res->Listen(a_Port))
	{
		cNetworkSingleton::Get().AddServer(res);
	}
	else
	{
		a_ListenCallbacks->OnError(res->m_ErrorCode, res->m_ErrorMsg);
	}
	return res;
}





bool cServerHandleImpl::Listen(UInt16 a_Port)
{
	// Make sure the cNetwork internals are innitialized:
	cNetworkSingleton::Get();

	// Set up the main socket:
	// It should listen on IPv6 with IPv4 fallback, when available; IPv4 when IPv6 is not available.
	bool NeedsTwoSockets = false;
	int err;
	evutil_socket_t MainSock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (!IsValidSocket(MainSock))
	{
		// Failed to create IPv6 socket, create an IPv4 one instead:
		err = EVUTIL_SOCKET_ERROR();
		LOGD("Failed to create IPv6 MainSock: %d (%s)", err, evutil_socket_error_to_string(err));
		MainSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (!IsValidSocket(MainSock))
		{
			m_ErrorCode = EVUTIL_SOCKET_ERROR();
			Printf(m_ErrorMsg, "Cannot create socket for port %d: %s", a_Port, evutil_socket_error_to_string(m_ErrorCode));
			return false;
		}

		// Bind to all interfaces:
		sockaddr_in name;
		memset(&name, 0, sizeof(name));
		name.sin_family = AF_INET;
		name.sin_port = ntohs(a_Port);
		if (bind(MainSock, reinterpret_cast<const sockaddr *>(&name), sizeof(name)) != 0)
		{
			m_ErrorCode = EVUTIL_SOCKET_ERROR();
			Printf(m_ErrorMsg, "Cannot bind IPv4 socket to port %d: %s", a_Port, evutil_socket_error_to_string(m_ErrorCode));
			evutil_closesocket(MainSock);
			return false;
		}
	}
	else
	{
		// IPv6 socket created, switch it into "dualstack" mode:
		UInt32 Zero = 0;
		#ifdef _WIN32
			// WinXP doesn't support this feature, so if the setting fails, create another socket later on:
			int res = setsockopt(MainSock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&Zero), sizeof(Zero));
			err = EVUTIL_SOCKET_ERROR();
			NeedsTwoSockets = ((res == SOCKET_ERROR) && (err == WSAENOPROTOOPT));
			LOGD("setsockopt(IPV6_V6ONLY) returned %d, err is %d (%s). %s",
				res, err, evutil_socket_error_to_string(err),
				NeedsTwoSockets ? "Second socket will be created" : "Second socket not needed"
			);
		#else
			setsockopt(MainSock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&Zero), sizeof(Zero));
		#endif

		// Bind to all interfaces:
		sockaddr_in6 name;
		memset(&name, 0, sizeof(name));
		name.sin6_family = AF_INET6;
		name.sin6_port = ntohs(a_Port);
		if (bind(MainSock, reinterpret_cast<const sockaddr *>(&name), sizeof(name)) != 0)
		{
			m_ErrorCode = EVUTIL_SOCKET_ERROR();
			Printf(m_ErrorMsg, "Cannot bind IPv6 socket to port %d: %s", a_Port, evutil_socket_error_to_string(m_ErrorCode));
			evutil_closesocket(MainSock);
			return false;
		}
	}
	if (evutil_make_socket_nonblocking(MainSock) != 0)
	{
		m_ErrorCode = EVUTIL_SOCKET_ERROR();
		Printf(m_ErrorMsg, "Cannot make socket on port %d non-blocking: %s", a_Port, evutil_socket_error_to_string(m_ErrorCode));
		evutil_closesocket(MainSock);
		return false;
	}
	if (listen(MainSock, 0) != 0)
	{
		m_ErrorCode = EVUTIL_SOCKET_ERROR();
		Printf(m_ErrorMsg, "Cannot listen on port %d: %s", a_Port, evutil_socket_error_to_string(m_ErrorCode));
		evutil_closesocket(MainSock);
		return false;
	}
	m_ConnListener = evconnlistener_new(cNetworkSingleton::Get().GetEventBase(), Callback, this, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 0, MainSock);

	// If a secondary socket is required (WinXP dual-stack), create it here:
	if (NeedsTwoSockets)
	{
		LOGD("Creating a second socket for IPv4");
		evutil_socket_t SecondSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (IsValidSocket(SecondSock))
		{
			if (evutil_make_socket_nonblocking(SecondSock) == 0)
			{
				m_SecondaryConnListener = evconnlistener_new(cNetworkSingleton::Get().GetEventBase(), Callback, this, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 0, SecondSock);
			}
			else
			{
				err = EVUTIL_SOCKET_ERROR();
				LOGD("evutil_make_socket_nonblocking() failed: %d, %s", err, evutil_socket_error_to_string(err));
			}
		}
		else
		{
			err = EVUTIL_SOCKET_ERROR();
			LOGD("socket(AF_INET, ...) failed: %d, %s", err, evutil_socket_error_to_string(err));
		}
	}
	m_IsListening = true;
	return true;
}





void cServerHandleImpl::Callback(evconnlistener * a_Listener, evutil_socket_t a_Socket, sockaddr * a_Addr, int a_Len, void * a_Self)
{
	// Cast to true self:
	cServerHandleImpl * Self = reinterpret_cast<cServerHandleImpl *>(a_Self);
	ASSERT(Self != nullptr);

	// Create a new cTCPLink for the incoming connection:
	cTCPLinkImplPtr Link = std::make_shared<cTCPLinkImpl>(a_Socket, Self->m_LinkCallbacks, Self, a_Addr, a_Len);
	{
		cCSLock Lock(Self->m_CS);
		Self->m_Connections.push_back(Link);
	}  // Lock(m_CS)

	// Call the OnAccepted callback:
	Self->m_ListenCallbacks->OnAccepted(*Link);
}





void cServerHandleImpl::RemoveLink(const cTCPLinkImpl * a_Link)
{
	cCSLock Lock(m_CS);
	for (auto itr = m_Connections.begin(), end = m_Connections.end(); itr != end; ++itr)
	{
		if (itr->get() == a_Link)
		{
			m_Connections.erase(itr);
			return;
		}
	}  // for itr - m_Connections[]
}





////////////////////////////////////////////////////////////////////////////////
// cNetwork:

bool cNetwork::Connect(
	const AString & a_Host,
	const UInt16 a_Port,
	cNetwork::cConnectCallbacksPtr a_ConnectCallbacks,
	cTCPLink::cCallbacksPtr a_LinkCallbacks
)
{
	// Add a connection request to the queue:
	cTCPLinkImplPtr Conn = cTCPLinkImpl::Connect(a_Host, a_Port, a_LinkCallbacks, a_ConnectCallbacks);
	return (Conn != nullptr);
}





cServerHandlePtr cNetwork::Listen(
	const UInt16 a_Port,
	cNetwork::cListenCallbacksPtr a_ListenCallbacks,
	cTCPLink::cCallbacksPtr a_LinkCallbacks
)
{
	return cServerHandleImpl::Listen(a_Port, a_ListenCallbacks, a_LinkCallbacks);
}





bool cNetwork::HostnameToIP(
	const AString & a_Hostname,
	cNetwork::cResolveNameCallbacksPtr a_Callbacks
)
{
	return cNetworkSingleton::Get().HostnameToIP(a_Hostname, a_Callbacks);
}





bool cNetwork::IPToHostName(
	const AString & a_IP,
	cNetwork::cResolveNameCallbacksPtr a_Callbacks
)
{
	return cNetworkSingleton::Get().IPToHostName(a_IP, a_Callbacks);
}




////////////////////////////////////////////////////////////////////////////////
// cTCPLink:

cTCPLink::cTCPLink(cCallbacksPtr a_Callbacks):
	m_Callbacks(a_Callbacks)
{
}




