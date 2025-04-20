#include "p2pnode.h"
#include <sstream>

NS_LOG_COMPONENT_DEFINE("P2PNode");

std::string Share::ToString() const
{
    std::stringstream ss;
    ss << "SHARE:" << originNodeId << ":" << shareId << ":" << timestamp;
    return ss.str();
}

Share Share::FromString(const std::string& str)
{
    Share share;

    size_t firstColon = str.find(":");
    size_t secondColon = str.find(":", firstColon + 1);
    size_t thirdColon = str.find(":", secondColon + 1);

    if (firstColon != std::string::npos && secondColon != std::string::npos &&
        thirdColon != std::string::npos)
    {
        share.originNodeId =
            std::stoul(str.substr(firstColon + 1, secondColon - firstColon - 1));
        share.shareId = std::stoul(str.substr(secondColon + 1, thirdColon - secondColon - 1));
        share.timestamp = std::stod(str.substr(thirdColon + 1));
    }

    return share;
}

P2PNode::P2PNode(uint32_t id)
    : id(id),
      sharesSent(0),
      sharesReceived(0),
      sharesGenerated(0),
      sharesForwarded(0)
{
    isrunning = false;
    std::random_device rd;
    rng.seed(rd() + id);
}

void P2PNode::SetupServerSocket(Ptr<Node> node)
{
    serverSocket = Socket::CreateSocket(node, TcpSocketFactory::GetTypeId());
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), id + 1000);
    serverSocket->Bind(local);
    serverSocket->Listen();
    serverSocket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
                                  MakeCallback(&P2PNode::HandleAccept, this));
}

void P2PNode::Stop()
{
    isrunning = false;

    if (serverSocket)
    {
        serverSocket->Close();
    }

    for (auto& peer : peersockets)
    {
        peer.second->Close();
    }
    peersockets.clear();
}

void P2PNode::HandleAccept(Ptr<Socket> socket, const Address& from)
{
    NS_LOG_INFO("Node " << id << " accepted connection from " << InetSocketAddress::ConvertFrom(from).GetIpv4());
    socket->SetRecvCallback(MakeCallback(&P2PNode::HandleRead, this));
}

void P2PNode::AddPeer(uint32_t peerId)
{
    if (std::find(peers.begin(), peers.end(), peerId) == peers.end())
    {
        peers.push_back(peerId);
    }
}

void P2PNode::AddPeerSocket(uint32_t peerId, Ptr<Socket> socket)
{
    peersockets[peerId] = socket;
    NS_LOG_INFO("Node " << id << " added socket connection to peer " << peerId);
}

void P2PNode::StartGeneratingShares()
{
    isrunning = true;
    ScheduleNextShare();
}

void P2PNode::ScheduleNextShare()
{
    std::uniform_real_distribution<double> dist(2.0, 5.0);
    double nextTime = dist(rng);

    shareEvent =
        Simulator::Schedule(Seconds(nextTime), &P2PNode::GenerateAndGossipShare, this);
}

void P2PNode::GenerateAndGossipShare()
{
    if (peers.empty())
    {
        NS_LOG_INFO("Node " << id << " has no peers to send shares to");
        ScheduleNextShare();
        return;
    }
    else if (!isrunning) return;
    Share share;
    share.originNodeId = id;
    share.shareId = GenerateUniqueShareId();
    sharesGenerated++;
    share.timestamp = Simulator::Now().GetSeconds();
    processedShares.insert(share.shareId);

    NS_LOG_INFO("Node " << id << " generating new share " << share.shareId);
    GossipShareToPeers(share);
    ScheduleNextShare();
}

void P2PNode::GossipShareToPeers(const Share& share)
{
    for (uint32_t peerId : peers)
    {
        auto it = peersockets.find(peerId);
        if (it == peersockets.end())
        {
            NS_LOG_INFO("Node " << id << " has no socket connection to peer " << peerId);
            continue;
        }
        Ptr<Socket> peerSocket = it->second;
        std::string shareMsg = share.ToString();
        Ptr<Packet> packet = Create<Packet>((uint8_t*)shareMsg.c_str(), shareMsg.length());
        int bytesSent = peerSocket->Send(packet);
        if (bytesSent > 0)
        {
            NS_LOG_INFO("Node " << id << " sending share " << share.originNodeId << ":"
                            << share.shareId << " to peer " << peerId);
            sharesSent++;
        }
        else
        {
            NS_LOG_INFO("Node " << id << " failed to send share to peer " << peerId);
            peersockets.erase(peerId);
        }
    }
}

void P2PNode::ReceiveShare(const std::string& shareMsg, Ptr<Socket> socket, const Address& from)
{
    if (shareMsg.find("REGISTER:") == 0)
    {
        size_t colonPos = shareMsg.find(":");
        if (colonPos != std::string::npos)
        {
            uint32_t peerId = std::stoul(shareMsg.substr(colonPos + 1));
            NS_LOG_INFO("Node " << id << " received registration from peer " << peerId);
            peersockets[peerId] = socket;
            peers.push_back(peerId);
            return;
        }
    }
    
    sharesReceived++;

    Share share = Share::FromString(shareMsg);
    if (processedShares.find(share.shareId) != processedShares.end())
    {
        NS_LOG_INFO("Node " << id << " already processed share " << share.originNodeId << ":"
                            << share.shareId);
        return;
    }
    processedShares.insert(share.shareId);

    NS_LOG_INFO("Node " << id << " received new share " << share.originNodeId << ":"
                        << share.shareId<<":"<<share.timestamp << " from origin " << share.originNodeId);

    sharesForwarded++;
    GossipShareToPeers(share);
}

void P2PNode::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        uint8_t* buffer = new uint8_t[packet->GetSize()];
        packet->CopyData(buffer, packet->GetSize());
        std::string msg = std::string((char*)buffer, packet->GetSize());
        delete[] buffer;
        
        ReceiveShare(msg, socket, from);
    }
}

uint32_t P2PNode::GenerateUniqueShareId()
{
    uint64_t seed = static_cast<uint64_t>(id) * 1000000 +
                    static_cast<uint64_t>(sharesGenerated) * 1000 +
                    static_cast<uint64_t>(Simulator::Now().GetTimeStep() % 1000);
    
    std::hash<uint64_t> hasher;
    return static_cast<uint32_t>(hasher(seed));
}

uint32_t P2PNode::GetId() const
{
    return id;
}

const std::vector<uint32_t>& P2PNode::GetPeers() const
{
    return peers;
}

uint32_t P2PNode::GetSharesSent() const
{
    return sharesSent;
}

uint32_t P2PNode::GetSharesReceived() const
{
    return sharesReceived;
}

uint32_t P2PNode::GetSharesGenerated() const
{
    return sharesGenerated;
}

uint32_t P2PNode::GetSharesForwarded() const
{
    return sharesForwarded;
}

size_t P2PNode::GetProcessedSharesCount() const
{
    return processedShares.size();
}

size_t P2PNode::GetPeerSocketsCount() const
{
    return peersockets.size();
}