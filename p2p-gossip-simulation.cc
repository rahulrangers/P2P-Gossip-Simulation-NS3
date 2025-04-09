#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("P2PGossipNetworkSimulation");

// Structure to represent a share
struct Share
{
    uint32_t originNodeId;
    uint32_t shareId;
    double timestamp;

    // For tracking propagation
    std::unordered_set<uint32_t> nodesVisited;

    std::string ToString() const
    {
        std::stringstream ss;
        ss << "SHARE:" << originNodeId << ":" << shareId << ":" << timestamp;
        return ss.str();
    }

    static Share FromString(const std::string& str)
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

    bool operator==(const Share& other) const
    {
        return originNodeId == other.originNodeId && shareId == other.shareId;
    }
};

// Hash function for Share struct
namespace std
{
template <>
struct hash<Share>
{
    size_t operator()(const Share& share) const
    {
        return hash<uint32_t>()(share.originNodeId) ^ hash<uint32_t>()(share.shareId);
    }
};
} // namespace std

class P2PNode
{
  private:
    uint32_t m_id;
    std::vector<uint32_t> m_peers;
    Ptr<Socket> m_socket;
    std::mt19937 m_rng;
    EventId m_shareEvent;

    // Keep track of shares we've seen to avoid re-processing
    std::unordered_set<Share> m_processedShares;

    uint32_t m_sharesSent;
    uint32_t m_sharesReceived;
    uint32_t m_sharesGenerated;
    uint32_t m_sharesForwarded;

  public:
    P2PNode(uint32_t id)
        : m_id(id),
          m_sharesSent(0),
          m_sharesReceived(0),
          m_sharesGenerated(0),
          m_sharesForwarded(0)
    {
        // Initialize random number generator with node-specific seed
        std::random_device rd;
        m_rng.seed(rd() + id);
    }

    void SetupSocket(Ptr<Node> node)
    {
        m_socket = Socket::CreateSocket(node, TcpSocketFactory::GetTypeId());

        // Bind to a local port (use node id + 1000 to avoid conflicts)
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_id + 1000);
        m_socket->Bind(local);
        m_socket->Listen();

        m_socket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
                                    MakeCallback(&P2PNode::HandleAccept, this));

        m_socket->SetRecvCallback(MakeCallback(&P2PNode::HandleRead, this));
    }

    void HandleAccept(Ptr<Socket> socket, const Address& from)
    {
        socket->SetRecvCallback(MakeCallback(&P2PNode::HandleRead, this));
    }

    void AddPeer(uint32_t peerId)
    {
        // Check if peer already exists
        if (std::find(m_peers.begin(), m_peers.end(), peerId) == m_peers.end())
        {
            m_peers.push_back(peerId);
        }
    }

    void StartGeneratingShares()
    {
        ScheduleNextShare();
    }

    void ScheduleNextShare()
    {
        // Generate a share every 2-5 seconds on average
        std::uniform_real_distribution<double> dist(2.0, 5.0);
        double nextTime = dist(m_rng);

        m_shareEvent =
            Simulator::Schedule(Seconds(nextTime), &P2PNode::GenerateAndGossipShare, this);
    }

    void GenerateAndGossipShare()
    {
        if (m_peers.empty())
        {
            NS_LOG_INFO("Node " << m_id << " has no peers to send shares to");
            ScheduleNextShare();
            return;
        }

        // Create a new share
        Share share;
        share.originNodeId = m_id;
        share.shareId = m_sharesGenerated++;
        share.timestamp = Simulator::Now().GetSeconds();
        // Mark that we've seen it
        share.nodesVisited.insert(m_id);

        // Add to processed shares to avoid receiving it back
        m_processedShares.insert(share);

        NS_LOG_INFO("Node " << m_id << " generating new share " << share.shareId);

        // Gossip to all peers
        GossipShareToPeers(share);

        // Schedule next share generation
        ScheduleNextShare();
    }

    void GossipShareToPeers(const Share& share)
    {
        for (uint32_t peerId : m_peers)
        {
            // Send share to peer
            NS_LOG_INFO("Node " << m_id << " sending share " << share.originNodeId << ":"
                                << share.shareId << " to peer " << peerId);

            // Send share through callback (will be handled in main simulation)
            m_onSendShare(peerId, share.ToString());
            m_sharesSent++;
        }
    }

    void ReceiveShare(const std::string& shareMsg)
    {
        m_sharesReceived++;

        Share share = Share::FromString(shareMsg);

        // Check if we've already processed this share
        if (m_processedShares.find(share) != m_processedShares.end())
        {
            NS_LOG_INFO("Node " << m_id << " already processed share " << share.originNodeId << ":"
                                << share.shareId);
            return;
        }

        // Mark as processed
        m_processedShares.insert(share);

        NS_LOG_INFO("Node " << m_id << " received new share " << share.originNodeId << ":"
                            << share.shareId << " from origin " << share.originNodeId);

        // Add ourselves to the nodes visited
        share.nodesVisited.insert(m_id);

        // Forward to peers (gossip protocol)
        m_sharesForwarded++;
        GossipShareToPeers(share);
    }

    // Socket event handlers
    void HandleRead(Ptr<Socket> socket)
    {
        Ptr<Packet> packet;
        Address from;
        while ((packet = socket->RecvFrom(from)))
        {
            uint8_t* buffer = new uint8_t[packet->GetSize()];
            packet->CopyData(buffer, packet->GetSize());
            std::string msg = std::string((char*)buffer, packet->GetSize());
            delete[] buffer;

            ReceiveShare(msg);
        }
    }

    // Send callback (will be set from main simulation)
    std::function<void(uint32_t, const std::string&)> m_onSendShare;

    // Getters
    uint32_t GetId() const
    {
        return m_id;
    }

    const std::vector<uint32_t>& GetPeers() const
    {
        return m_peers;
    }

    uint32_t GetSharesSent() const
    {
        return m_sharesSent;
    }

    uint32_t GetSharesReceived() const
    {
        return m_sharesReceived;
    }

    uint32_t GetSharesGenerated() const
    {
        return m_sharesGenerated;
    }

    uint32_t GetSharesForwarded() const
    {
        return m_sharesForwarded;
    }

    size_t GetProcessedSharesCount() const
    {
        return m_processedShares.size();
    }
};

class P2PGossipNetworkSimulation
{
  private:
    NodeContainer m_nodes;
    InternetStackHelper m_internet;
    Ipv4AddressHelper m_addressHelper;
    std::vector<std::shared_ptr<P2PNode>> m_p2pNodes;

    // Store the connection info for each pair of nodes
    struct ConnectionInfo
    {
        NetDeviceContainer devices;
        Ptr<PointToPointChannel> channel;
    };

    std::map<std::pair<uint32_t, uint32_t>, ConnectionInfo> m_connections;

    uint32_t m_totalMessagesSent;
    uint32_t m_totalMessagesReceived;

    // NetAnim animator
    AnimationInterface* m_anim;

  public:
    P2PGossipNetworkSimulation(uint32_t numNodes)
        : m_totalMessagesSent(0),
          m_totalMessagesReceived(0),
          m_anim(nullptr)
    {
        // Create nodes
        m_nodes.Create(numNodes);

        // Install internet stack
        m_internet.Install(m_nodes);

        // Set up P2P nodes
        for (uint32_t i = 0; i < numNodes; i++)
        {
            std::shared_ptr<P2PNode> node = std::make_shared<P2PNode>(i);
            m_p2pNodes.push_back(node);
        }

        // Set up send callbacks
        for (auto& node : m_p2pNodes)
        {
            node->m_onSendShare = [this](uint32_t peerId, const std::string& msg) {
                this->SendShareToPeer(peerId, msg);
            };
        }
    }

    ~P2PGossipNetworkSimulation()
    {
        // Clean up the animator
        if (m_anim)
        {
            delete m_anim;
        }
    }

    void CreateRandomTopology(double connectionProbability = 0.3, double latency = 5.0)
    {
        uint32_t numNodes = m_nodes.GetN();

        // Set up random number generator
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_real_distribution<double> dist(0.0, 1.0);

        // Create connections between nodes with probability
        for (uint32_t i = 0; i < numNodes; i++)
        {
            for (uint32_t j = i + 1; j < numNodes; j++)
            {
                if (dist(rng) < connectionProbability)
                {
                    ConnectNodes(i, j,latency);
                }
            }
        }

        // Make sure all nodes have at least one connection
        for (uint32_t i = 0; i < numNodes; i++)
        {
            if (m_p2pNodes[i]->GetPeers().empty())
            {
                std::uniform_int_distribution<uint32_t> nodeDist(0, numNodes - 1);
                uint32_t j = nodeDist(rng);
                while (j == i)
                {
                    j = nodeDist(rng);
                }
                ConnectNodes(i, j, latency);

                NS_LOG_INFO("Created additional link between nodes "
                            << i << " and " << j << " with latency " << latency << "ms");
            }
        }

        // Set up sockets for each node
        for (uint32_t i = 0; i < numNodes; i++)
        {
            m_p2pNodes[i]->SetupSocket(m_nodes.Get(i));
        }

        // Enable routing
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    void ConnectNodes(uint32_t i, uint32_t j, double latencyMs)
    {
        // Create point-to-point helper with specific attributes
        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
        p2pHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(latencyMs)));

        // Create link
        NodeContainer linkNodes;
        linkNodes.Add(m_nodes.Get(i));
        linkNodes.Add(m_nodes.Get(j));
        NetDeviceContainer linkDevices = p2pHelper.Install(linkNodes);

        // Store the connection info
        ConnectionInfo connInfo;
        connInfo.devices = linkDevices;
        connInfo.channel = linkDevices.Get(0)->GetChannel()->GetObject<PointToPointChannel>();
        m_connections[std::make_pair(i, j)] = connInfo;

        // Assign addresses
        std::stringstream ss;
        ss << "10." << (i + 1) << "." << (j + 1) << ".0";
        m_addressHelper.SetBase(ss.str().c_str(), "255.255.255.0");
        m_addressHelper.Assign(linkDevices);

        // Add peers to P2P nodes
        m_p2pNodes[i]->AddPeer(j);
        m_p2pNodes[j]->AddPeer(i);

        NS_LOG_INFO("Created link between nodes " << i << " and " << j << " with latency "
                                                  << latencyMs << "ms");
    }

    void SetupNetAnim()
    {
        // Create the animator instance
        m_anim = new AnimationInterface("p2p-gossip-tcp-animation.xml");

        // Configure animation parameters
        m_anim->SetConstantPosition(m_nodes.Get(0), 0.0, 0.0); 

        // Place nodes in a grid layout
        uint32_t numNodes = m_nodes.GetN();
        int gridSize = std::ceil(std::sqrt(numNodes));

        for (uint32_t i = 0; i < numNodes; i++)
        {
            int row = i / gridSize;
            int col = i % gridSize;
            m_anim->SetConstantPosition(m_nodes.Get(i), 100.0 * col, 100.0 * row);

            // Update node description
            std::ostringstream desc;
            desc << "Node " << i;
            m_anim->UpdateNodeDescription(m_nodes.Get(i), desc.str());

            size_t degree = m_p2pNodes[i]->GetPeers().size();
            if (degree > 4)
            {
                m_anim->UpdateNodeColor(m_nodes.Get(i), 255, 0, 0); // Red for high degree
            }
            else if (degree > 2)
            {
                m_anim->UpdateNodeColor(m_nodes.Get(i), 0, 255, 0); // Green for medium degree
            }
            else
            {
                m_anim->UpdateNodeColor(m_nodes.Get(i), 0, 0, 255); // Blue for low degree
            }
        }

        m_anim->EnablePacketMetadata(true);

        NS_LOG_INFO("NetAnim configured to save in p2p-gossip-tcp-animation.xml");
    }

    void Start(double simulationTime = 100.0, double statsInterval = 10.0)
    {
        // Setup NetAnim visualization
        SetupNetAnim();

        // Start share generation on all nodes
        for (auto& node : m_p2pNodes)
        {
            node->StartGeneratingShares();
        }

        // Schedule periodic statistics
        for (double t = statsInterval; t < simulationTime; t += statsInterval)
        {
            Simulator::Schedule(Seconds(t), &P2PGossipNetworkSimulation::PrintPeriodicStats, this);
        }

        // Schedule to print final statistics at the end
        Simulator::Schedule(Seconds(simulationTime - 0.1),
                            &P2PGossipNetworkSimulation::PrintStatistics,
                            this);

        // Run simulation
        NS_LOG_INFO("Starting gossip network simulation for " << simulationTime << " seconds");
        Simulator::Stop(Seconds(simulationTime));
        Simulator::Run();
        Simulator::Destroy();
    }

    void SendShareToPeer(uint32_t peerId, const std::string& message)
    {
        m_totalMessagesSent++;

        // Create packet
        Ptr<Packet> packet = Create<Packet>((uint8_t*)message.c_str(), message.length());

        // We need the source node ID to find the connection
        size_t firstColon = message.find(":");
        size_t secondColon = message.find(":", firstColon + 1);
        uint32_t sourceNodeId =
            std::stoul(message.substr(firstColon + 1, secondColon - firstColon - 1));

        // Find the socket to use
        Ptr<Node> destinationNode = m_nodes.Get(peerId);

        // Create a socket for sending
        Ptr<Socket> socket =
            Socket::CreateSocket(m_nodes.Get(sourceNodeId), TcpSocketFactory::GetTypeId());

        // Get destination address
        Ptr<Ipv4> ipv4 = destinationNode->GetObject<Ipv4>();
        Ipv4InterfaceAddress destAddr;

        // Find the right interface
        for (uint32_t i = 1; i < ipv4->GetNInterfaces(); i++)
        {
            for (uint32_t j = 0; j < ipv4->GetNAddresses(i); j++)
            {
                destAddr = ipv4->GetAddress(i, j);

                if (destAddr.GetLocal() != Ipv4Address("127.0.0.1"))
                {
                    break;
                }
            }
        }

        // Connect to the destination node's socket
        socket->Connect(InetSocketAddress(destAddr.GetLocal(), peerId + 1000));

        // Send the packet
        socket->Send(packet);
        socket->Close();
    }

    void PrintPeriodicStats()
    {
        double simTime = Simulator::Now().GetSeconds();
        NS_LOG_INFO("=== Periodic Stats at " << simTime << "s ===");

        // Calculate total shares in the network
        uint32_t totalShares = 0;
        uint32_t totalGenerated = 0;

        for (const auto& node : m_p2pNodes)
        {
            totalShares += node->GetProcessedSharesCount();
            totalGenerated += node->GetSharesGenerated();
        }

        NS_LOG_INFO("Total shares generated: " << totalGenerated);
        NS_LOG_INFO("Average shares per node: " << (totalShares / m_p2pNodes.size()));
        NS_LOG_INFO("Network messages sent: " << m_totalMessagesSent);

    }

    void PrintStatistics()
    {
        NS_LOG_INFO("=== P2P Gossip Network Simulation Statistics ===");
        NS_LOG_INFO("Total messages sent across network: " << m_totalMessagesSent);

        uint32_t totalSharesReceived = 0;
        uint32_t totalSharesGenerated = 0;
        uint32_t totalSharesForwarded = 0;

        for (const auto& node : m_p2pNodes)
        {
            totalSharesReceived += node->GetSharesReceived();
            totalSharesGenerated += node->GetSharesGenerated();
            totalSharesForwarded += node->GetSharesForwarded();

            NS_LOG_INFO("Node " << node->GetId() << ": Generated " << node->GetSharesGenerated()
                                << ", Received " << node->GetSharesReceived() << ", Forwarded "
                                << node->GetSharesForwarded() << ", Total processed "
                                << node->GetProcessedSharesCount() << ", Peer count "
                                << node->GetPeers().size());
        }

        NS_LOG_INFO("Total shares generated: " << totalSharesGenerated);
        NS_LOG_INFO("Total shares received: " << totalSharesReceived);
        NS_LOG_INFO("Total shares forwarded: " << totalSharesForwarded);
    }
};

int
main(int argc, char* argv[])
{
    LogComponentEnable("P2PGossipNetworkSimulation", LOG_LEVEL_INFO);

    // Default parameters
    uint32_t numNodes = 12;
    double connectionProbability = 0.3;
    double simulationTime = 60.0; 
    double LatencyMs = 5.0;

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("numNodes", "Number of nodes", numNodes);
    cmd.AddValue("connectionProb",
                 "Probability of connection between nodes",
                 connectionProbability);
    cmd.AddValue("simTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("Latency", " latency in ms", LatencyMs);
    cmd.Parse(argc, argv);

    // Create and run simulation
    P2PGossipNetworkSimulation sim(numNodes);
    sim.CreateRandomTopology(connectionProbability, LatencyMs);
    sim.Start(simulationTime);

    return 0;
}