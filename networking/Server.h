#pragma once
#include "ServerStatistics.h"

#include "evtc_rpc_messages.h"

#include <chrono>

class ConnectionContext;

class ConnectionInterface
{
public:
	virtual ~ConnectionInterface() = default;
	virtual void SendEventToClient(
		const cbtevent& pEvent,
		uint16_t pSenderInstanceId,
		const std::shared_ptr<ConnectionContext>& pConnectionContext,
		const ConnectionContext* pSenderForLogging) = 0;
	virtual void ForceDisconnect(
		const char* pErrorMessage,
		const std::shared_ptr<ConnectionContext>& pClient,
		bool pRemovedFromTable) = 0;
};

class ConnectionContext
{
public:
	explicit ConnectionContext(std::unique_ptr<ConnectionInterface>&& pInterface);

	std::optional<std::map<std::string, std::shared_ptr<ConnectionContext>>::iterator> Iterator{}; // Protected by mRegisteredAgentsLock on the server that owns this ConnectionContext
	uint16_t InstanceId = 0; // Protected by mRegisteredAgentsLock on the server that owns this ConnectionContext
	std::map<std::string, uint16_t> Peers; // Protected by mRegisteredAgentsLock on the server that owns this ConnectionContext

	std::atomic<std::chrono::steady_clock::time_point> LastCallTime;
	std::atomic<bool> ForceDisconnected{false};

	const std::unique_ptr<ConnectionInterface> Interface;
};

class evtc_rpc_server
{
	enum class ShutdownState
	{
		Online,
		ShouldShutdown,
		ShuttingDown
	};

public:
	evtc_rpc_server(const char* pPrometheusEndpoint);
	~evtc_rpc_server();

	ServerStatisticsSample GetStatistics();
	ServerStatistics& SubmitStatistics();

	void ForceDisconnect(const char* pErrorMessage, const std::shared_ptr<ConnectionContext>& pClient);
	void HandleIncomingMessage(const void* pData, size_t pLen, std::shared_ptr<ConnectionContext>& pClient);

#ifndef TEST
private:
#endif
	const char* HandleRegisterSelf(uint16_t pInstanceId, std::string_view pAccountName, std::shared_ptr<ConnectionContext>& pClient);
	const char* HandleSetSelfId(uint16_t pInstanceId, std::shared_ptr<ConnectionContext>& pClient);
	const char* HandleAddPeer(uint16_t pInstanceId, std::string_view pAccountName, std::shared_ptr<ConnectionContext>& pClient);
	const char* HandleRemovePeer(uint16_t pInstanceId, std::shared_ptr<ConnectionContext>& pClient);
	const char* HandleCombatEvent(const cbtevent& pEvent, std::shared_ptr<ConnectionContext>& pClient);

	std::mutex mRegisteredAgentsLock;
	std::map<std::string, std::shared_ptr<ConnectionContext>> mRegisteredAgents;

	std::shared_ptr<ServerStatistics> mStatistics;
	prometheus::Exposer mPrometheusExposer;
	
	std::atomic<uint64_t> mConflictingClientDisconnectThresholdMs = 30000;
};
