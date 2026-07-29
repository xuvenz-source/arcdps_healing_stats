#include "Server.h"

#include "../src/Log.h"

ConnectionContext::ConnectionContext(std::unique_ptr<ConnectionInterface>&& pInterface)
	: Interface(std::move(pInterface))
{}

evtc_rpc_server::evtc_rpc_server(const char* pPrometheusEndpoint)
{
	mStatistics = std::make_shared<ServerStatistics>(*this);
	if (pPrometheusEndpoint != nullptr)
	{
		mPrometheusExposer.emplace(pPrometheusEndpoint);
		mPrometheusExposer->RegisterCollectable(mStatistics->PrometheusRegistry);
		mPrometheusExposer->RegisterCollectable(mStatistics);
	}

	LogI("Started - pPrometheusEndpoint={}", pPrometheusEndpoint);
}

evtc_rpc_server::~evtc_rpc_server()
{
	assert(mRegisteredAgents.size() == 0);
}

ServerStatisticsSample evtc_rpc_server::GetStatistics()
{
	ServerStatisticsSample result = {};

	std::lock_guard agents_lock{ mRegisteredAgentsLock };
	result.RegisteredPlayers = mRegisteredAgents.size();

	for (const auto& agent : mRegisteredAgents)
	{
		result.KnownPeers += agent.second->Peers.size();
		for (const auto& peer : agent.second->Peers)
		{
			const auto iter = mRegisteredAgents.find(peer.first);
			if (iter != mRegisteredAgents.end())
			{
				result.RegisteredPeers += 1;
			}
		}
	}

	return result;
}

ServerStatistics& evtc_rpc_server::SubmitStatistics()
{
	return *mStatistics;
}

void evtc_rpc_server::HandleIncomingMessage(const void* pData, size_t pLen, std::shared_ptr<ConnectionContext>& pClient)
{
	using namespace evtc_rpc::messages;

	if (pLen < sizeof(Header))
	{
		LogE("(client {}) data too short for header ({} vs {})", fmt::ptr(pClient.get()), pLen, sizeof(Header));
		ForceDisconnect("short message header", pClient);
		return;
	}
	
	const char* data = reinterpret_cast<const char*>(pData);
	Header header;
	memcpy(&header, data, sizeof(Header));
	data += sizeof(Header);
	pLen -= sizeof(Header);

	if (header.MessageVersion != 1)
	{
		LogE("(client {}) incorrect version {}", fmt::ptr(pClient.get()), header.MessageVersion);
		ForceDisconnect("incorrect version", pClient);
		return;
	}

	if (header.MessageType < evtc_rpc::messages::Type::Max)
	{
		mStatistics->MessageTypeReceive[static_cast<size_t>(header.MessageType)]->Increment();
	}

	switch (header.MessageType)
	{
	case Type::RegisterSelf:
	{
		if (pLen < sizeof(RegisterSelf))
		{
			LogE("(client {}) data too short for RegisterSelf message ({} vs {})",
				fmt::ptr(pClient.get()), pLen, sizeof(RegisterSelf));
			ForceDisconnect("short RegisterSelf content", pClient);
			return;
		}

		RegisterSelf message;
		memcpy(&message, data, sizeof(RegisterSelf));
		data += sizeof(RegisterSelf);
		pLen -= sizeof(RegisterSelf);

		if (pLen != message.SelfAccountNameLength)
		{
			LogE("(client {}) incorrect RegisterSelf name length ({} vs {})",
				fmt::ptr(pClient.get()), pLen, message.SelfAccountNameLength);
			ForceDisconnect("mismatched RegisterSelf length", pClient);
			return;
		}

		const char* error = HandleRegisterSelf(message.SelfId, std::string_view{data, message.SelfAccountNameLength}, pClient);
		if (error != nullptr)
		{
			LogE("(client {}) HandleRegisterSelf failed - {}", fmt::ptr(pClient.get()), error);
			ForceDisconnect(error, pClient);
			return;
		}
		break;
	}
	case Type::SetSelfId:
	{
		if (pLen != sizeof(SetSelfId))
		{
			LogE("(client {}) data length mismatch for SetSelfId message ({} vs {})",
				fmt::ptr(pClient.get()), pLen, sizeof(SetSelfId));
			ForceDisconnect("short SetSelfId content", pClient);
			return;
		}

		SetSelfId message;
		memcpy(&message, data, sizeof(SetSelfId));
		data += sizeof(SetSelfId);
		pLen -= sizeof(SetSelfId);

		const char* error = HandleSetSelfId(message.SelfId, pClient);
		if (error != nullptr)
		{
			LogW("(client {}) HandleSetSelfId failed - {}", fmt::ptr(pClient.get()), error);
			ForceDisconnect(error, pClient);
			return;
		}
		break;
	}
	case Type::AddPeer:
	{
		if (pLen < sizeof(AddPeer))
		{
			LogE("(client {}) data too short for AddPeer message ({} vs {})",
				fmt::ptr(pClient.get()), pLen, sizeof(AddPeer));
			ForceDisconnect("short AddPeer content", pClient);
			return;
		}

		AddPeer message;
		memcpy(&message, data, sizeof(AddPeer));
		data += sizeof(AddPeer);
		pLen -= sizeof(AddPeer);

		if (pLen != message.PeerAccountNameLength)
		{
			LogE("(client {}) incorrect AddPeer name length ({} vs {})",
				fmt::ptr(pClient.get()), pLen, message.PeerAccountNameLength);
			ForceDisconnect("mismatched AddPeer length", pClient);
			return;
		}

		const char* error = HandleAddPeer(message.PeerId, std::string_view{data, message.PeerAccountNameLength}, pClient);
		if (error != nullptr)
		{
			LogW("(client {}) HandleAddPeer failed - {}", fmt::ptr(pClient.get()), error);
			ForceDisconnect(error, pClient);
			return;
		}
		break;
	}
	case Type::RemovePeer:
	{
		if (pLen != sizeof(RemovePeer))
		{
			LogE("(client {}) data length mismatch for RemovePeer message ({} vs {})",
				fmt::ptr(pClient.get()), pLen, sizeof(RemovePeer));
			ForceDisconnect("RemovePeer size mismatch", pClient);
			return;
		}

		RemovePeer message;
		memcpy(&message, data, sizeof(RemovePeer));
		data += sizeof(RemovePeer);
		pLen -= sizeof(RemovePeer);

		const char* error = HandleRemovePeer(message.PeerId, pClient);
		if (error != nullptr)
		{
			LogW("(client {}) HandleRemovePeer failed - {}", fmt::ptr(pClient.get()), error);
			ForceDisconnect(error, pClient);
			return;
		}
		break;
	}
	case Type::CombatEvent:
	{
		if (pLen != sizeof(CombatEvent))
		{
			LogE("(client {}) data length mismatch for CombatEvent message ({} vs {})",
				fmt::ptr(pClient.get()), pLen, sizeof(CombatEvent));
			ForceDisconnect("CombatEvent size mismatch", pClient);
			return;
		}

		CombatEvent message;
		memcpy(&message, data, sizeof(CombatEvent));
		data += sizeof(CombatEvent);
		pLen -= sizeof(CombatEvent);

		const char* error = HandleCombatEvent(message.Event, pClient);
		if (error != nullptr)
		{
			LogW("(client {}) HandleCombatEvent failed - {}", fmt::ptr(pClient.get()), error);
			ForceDisconnect(error, pClient);
			return;
		}
		break;
	}

	default:
		LogE("(client {}) incorrect type {}", fmt::ptr(pClient.get()), static_cast<int>(header.MessageType));
		return;
	}
	
	pClient->LastCallTime.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

const char* evtc_rpc_server::HandleRegisterSelf(uint16_t pInstanceId, std::string_view pAccountName, std::shared_ptr<ConnectionContext>& pClient)
{
	std::lock_guard lock(mRegisteredAgentsLock);

	if (pClient->ForceDisconnected.load(std::memory_order_acquire) == true)
	{
		LogD("client is already disconnected");
		return "client is already disconnected";
	}

	// Check this under lock, mRegisteredAgentsLock also guards the ReceivedAccountName flag
	if (pClient->Iterator.has_value())
	{
		LogE("(client {}) this connection already has a registered account name", fmt::ptr(pClient.get()));
		return "already registered account name on this connection";
	}

	std::string accountName{pAccountName};
	auto [newEntry, inserted] = mRegisteredAgents.try_emplace(std::move(accountName), std::shared_ptr{pClient});
	if (inserted == false)
	{
		std::chrono::steady_clock::time_point lastCallTime = newEntry->second->LastCallTime.load(std::memory_order_relaxed);
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		uint64_t millisecondsSinceLastCall = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCallTime).count();
		LogW("(client {}) account name {} is already registered from another connection {} ({} since last call)",
			fmt::ptr(pClient.get()), accountName.c_str(), fmt::ptr(newEntry->second.get()), millisecondsSinceLastCall);
		if (millisecondsSinceLastCall < mConflictingClientDisconnectThresholdMs.load(std::memory_order_relaxed))
		{
			return "account name collision";
		}

		std::shared_ptr<ConnectionContext> oldClient = std::move(newEntry->second);
		oldClient->Iterator.reset();
		LogW("Force disconnect of old client {}", fmt::ptr(oldClient.get()));
		oldClient->Interface->ForceDisconnect("superseded by new client", oldClient, true);

		newEntry->second = pClient;
	}

	pClient->Iterator = newEntry;
	pClient->InstanceId = pInstanceId;

	LogI("(client {}) registered account {} {}", fmt::ptr(pClient.get()), newEntry->first.c_str(), newEntry->second->InstanceId);
	return nullptr;
}

const char* evtc_rpc_server::HandleSetSelfId(uint16_t pInstanceId, std::shared_ptr<ConnectionContext>& pClient)
{
	std::lock_guard lock(mRegisteredAgentsLock);

	// Check this under lock, mRegisteredAgentsLock also guards the ReceivedAccountName flag
	if (pClient->Iterator.has_value() == false)
	{
		LogE("(client {}) this connection is not registered yet", fmt::ptr(pClient.get()));
		return "not registered yet";
	}

	pClient->InstanceId = pInstanceId;

	LogI("(client {}) set self id to {}", fmt::ptr(pClient.get()), pInstanceId);
	return nullptr;
}

const char* evtc_rpc_server::HandleAddPeer(uint16_t pInstanceId, std::string_view pAccountName, std::shared_ptr<ConnectionContext>& pClient)
{
	std::lock_guard lock(mRegisteredAgentsLock);

	// Check this under lock, mRegisteredAgentsLock also guards the ReceivedAccountName flag
	if (pClient->Iterator.has_value() == false)
	{
		LogE("(client {}) this connection is not registered yet", fmt::ptr(pClient.get()));
		return "not registered yet";
	}

	std::string accountName{pAccountName};
	auto [newEntry, inserted] = pClient->Peers.try_emplace(std::move(accountName), pInstanceId);
	if (inserted == false)
	{
		LogW("(client {}) peer {} is already registered (instance id {}, new instance id is {}). Overriding existing peer.", fmt::ptr(pClient.get()), accountName.c_str(), newEntry->second, pInstanceId);
		newEntry->second = pInstanceId;
	}

	LogI("(client {}) added peer {} {}", fmt::ptr(pClient.get()), newEntry->first.c_str(), newEntry->second);
	return nullptr;
}

const char* evtc_rpc_server::HandleRemovePeer(uint16_t pInstanceId, std::shared_ptr<ConnectionContext>& pClient)
{
	std::lock_guard lock(mRegisteredAgentsLock);

	// Check this under lock, mRegisteredAgentsLock also guards the ReceivedAccountName flag
	if (pClient->Iterator.has_value() == false)
	{
		LogE("(client {}) this connection is not registered yet", fmt::ptr(pClient.get()));
		return "not registered yet";
	}

	std::string removed_name = "";
	for (auto iter = pClient->Peers.begin(); iter != pClient->Peers.end(); iter++)
	{
		if (iter->second == pInstanceId)
		{
			removed_name = iter->first;
			pClient->Peers.erase(iter);
			break;
		}
	}
	
	if (removed_name == "")
	{
		LogI("(client {}) can't find peer with instance id {}", fmt::ptr(pClient.get()), pInstanceId);
		return nullptr;
	}

	LogI("(client {}) removed peer {} {}", fmt::ptr(pClient.get()), removed_name.c_str(), pInstanceId);
	return nullptr;
}

const char* evtc_rpc_server::HandleCombatEvent(const cbtevent& pEvent, std::shared_ptr<ConnectionContext>& pClient)
{
	uint16_t instanceId = 0;
	std::vector<std::shared_ptr<ConnectionContext>> peers; 
	{
		std::lock_guard lock(mRegisteredAgentsLock);
		// Check this under lock, mRegisteredAgentsLock also guards the ReceivedAccountName flag
		if (pClient->Iterator.has_value() == false)
		{
			LogE("(client {}) this connection is not registered yet", fmt::ptr(pClient.get()));
			return "not registered yet";
		}

		instanceId = pClient->InstanceId;

		for (const auto& [peerName, peerId] : pClient->Peers)
		{
			auto iter = mRegisteredAgents.find(peerName);
			if (iter != mRegisteredAgents.end())
			{
				if (iter->second->InstanceId != peerId)
				{
					LogT("(client {}) peer {} has incorrect instance id (expected {}, found {})", fmt::ptr(pClient.get()), fmt::ptr(iter->second.get()), iter->second->InstanceId, peerId);
					continue;
				}

				peers.emplace_back(std::shared_ptr<ConnectionContext>(iter->second));
			}
		}
	}

	// We know that all peers are registered at this point since we got them from the registered agents map
	for (const auto& peer : peers)
	{
		peer->Interface->SendEventToClient(pEvent, instanceId, peer, pClient.get());
	}

	LogD("(client {}) Queued CombatEvents to {} peers", fmt::ptr(pClient.get()), peers.size());

	return nullptr;
}

void evtc_rpc_server::ForceDisconnect(const char* pErrorMessage, const std::shared_ptr<ConnectionContext>& pClient)
{
	std::lock_guard lock(mRegisteredAgentsLock);

	bool removedFromTable = false;
	{
		if (pClient->Iterator.has_value())
		{
			mRegisteredAgents.erase(*pClient->Iterator);
			pClient->Iterator.reset();
			removedFromTable = true;
		}
	}
	
	pClient->Interface->ForceDisconnect(pErrorMessage, pClient, removedFromTable);
}
