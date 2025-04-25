#include "p2pnode.h"

#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/point-to-point-module.h"

#include <cmath>
#include <map>
#include <memory>

NS_LOG_COMPONENT_DEFINE("P2PGossipNetworkSimulation");

using namespace ns3;

class P2PGossipNetworkSimulation
{
  private:
    NodeContainer nodes;
    InternetStackHelper internet;
    Ipv4AddressHelper addressHelper;
    std::vector<std::shared_ptr<P2PNode>> p2pNodes;

    struct ConnectionInfo
    {
        NetDeviceContainer devices;
        Ptr<PointToPointChannel> channel;
        Ipv4InterfaceContainer ifc;
    };

    std::map<std::pair<uint32_t, uint32_t>, ConnectionInfo> connections;

    uint32_t totalMessagesSent;
    uint32_t totalMessagesReceived;

    // NetAnim animator
    AnimationInterface* anim;

  public:
    // Constructor: Creates the network with the specified number of nodes
    P2PGossipNetworkSimulation(uint32_t numNodes)
    {
        nodes.Create(numNodes);
        internet.Install(nodes);

        for (uint32_t i = 0; i < numNodes; i++)
        {
            std::shared_ptr<P2PNode> node = std::make_shared<P2PNode>(i);
            p2pNodes.push_back(node);
        }
    }

    // Destructor: Cleans up animation resources
    ~P2PGossipNetworkSimulation()
    {
        if (anim)
        {
            delete anim;
        }
    }

    // Creates a random network topology with given connection probability and latency
    void CreateRandomTopology(double connectionProbability = 0.3, double latency = 5.0)
    {
        uint32_t numNodes = nodes.GetN();
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_real_distribution<double> dist(0.0, 1.0);

        for (uint32_t i = 0; i < numNodes; i++)
        {
            bool connected = false;
            for (uint32_t j = i + 1; j < numNodes; j++)
            {
                if (dist(rng) < connectionProbability)
                {
                    connected = true;
                    ConnectNodes(i, j, latency);
                }
            }

            if(!connected){
                if(i==0 && numNodes>0)  ConnectNodes(0, 1, latency);
                else ConnectNodes(i,i-1,latency);
            }
        }

        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
        for (uint32_t i = 0; i < numNodes; i++)
        {
            p2pNodes[i]->SetupServerSocket(nodes.Get(i));
        }

        Simulator::Schedule(Seconds(5) + Simulator::Now(),
                            &P2PGossipNetworkSimulation::makeconnections,
                            this);
    }

    // Establishes socket connections between all connected node pairs
    void makeconnections()
    {
        for (const auto& connection : connections)
        {
            uint32_t i = connection.first.first;
            uint32_t j = connection.first.second;
            ConnectPeerSockets(i, j);
        }
    }

    // Creates a physical connection between two nodes with the given latency
    void ConnectNodes(uint32_t i, uint32_t j, double latencyMs)
    {
        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
        p2pHelper.SetChannelAttribute("Delay", TimeValue(MilliSeconds(latencyMs)));
        NodeContainer linkNodes;
        linkNodes.Add(nodes.Get(i));
        linkNodes.Add(nodes.Get(j));
        NetDeviceContainer linkDevices = p2pHelper.Install(linkNodes);

        std::ostringstream network;
        network << "10." << (i + 1) << "." << (j + 1) << ".0";
        addressHelper.SetBase(Ipv4Address(network.str().c_str()), Ipv4Mask("255.255.255.0"));

        Ipv4InterfaceContainer ifc = addressHelper.Assign(linkDevices);
        ConnectionInfo connInfo;
        connInfo.devices = linkDevices;
        connInfo.channel = linkDevices.Get(0)->GetChannel()->GetObject<PointToPointChannel>();
        connInfo.ifc = ifc;
        connections[std::make_pair(i, j)] = connInfo;
    }

    // Sets up TCP socket connections between node i and node j
    void ConnectPeerSockets(uint32_t i, uint32_t j)
    {
        auto& conn = connections[{i, j}];
        Ipv4Address addrJ = conn.ifc.GetAddress(1);

        Ptr<Socket> socket = Socket::CreateSocket(nodes.Get(i), TcpSocketFactory::GetTypeId());

        socket->Connect(InetSocketAddress(addrJ, j + 1000));

        socket->SetRecvCallback(MakeCallback(&P2PNode::HandleRead, p2pNodes[i].get()));

        p2pNodes[i]->AddPeerSocket(j, socket);
        p2pNodes[i]->AddPeer(j);
        std::string reg = "REGISTER:" + std::to_string(i);
        Ptr<Packet> packet =
            Create<Packet>(reinterpret_cast<const uint8_t*>(reg.c_str()), reg.length());
        socket->Send(packet);
    }

    // Configures the NetAnim visualization for the network
    void SetupNetAnim()
    {
        anim = new AnimationInterface("p2p-gossip-tcp-animation.xml");

        anim->SetConstantPosition(nodes.Get(0), 0.0, 0.0);

        uint32_t numNodes = nodes.GetN();
        int gridSize = std::ceil(std::sqrt(numNodes));

        for (uint32_t i = 0; i < numNodes; i++)
        {
            int row = i / gridSize;
            int col = i % gridSize;
            anim->SetConstantPosition(nodes.Get(i), 100.0 * col, 100.0 * row);

            std::ostringstream desc;
            desc << "Node " << i;
            anim->UpdateNodeDescription(nodes.Get(i), desc.str());

            size_t degree = p2pNodes[i]->GetPeers().size();
            if (degree > 4)
            {
                anim->UpdateNodeColor(nodes.Get(i), 255, 0, 0);
            }
            else if (degree > 2)
            {
                anim->UpdateNodeColor(nodes.Get(i), 0, 255, 0);
            }
            else
            {
                anim->UpdateNodeColor(nodes.Get(i), 0, 0, 255);
            }
        }

        anim->EnablePacketMetadata(true);

        NS_LOG_INFO("NetAnim configured to save in p2p-gossip-tcp-animation.xml");
    }

    // Starts the simulation and runs it for the specified time with periodic statistics
    void Start(double simulationTime = 100.0, double statsInterval = 10.0)
    {
        SetupNetAnim();
        for (auto& node : p2pNodes)
        {
            node->StartGeneratingShares();
        }

        for (double t = statsInterval; t < simulationTime; t += statsInterval)
        {
            Simulator::Schedule(Seconds(t), &P2PGossipNetworkSimulation::PrintPeriodicStats, this);
        }

        Simulator::Schedule(Seconds(simulationTime - 0.1),
                            &P2PGossipNetworkSimulation::PrintStatistics,
                            this);

        Simulator::Schedule(Seconds(simulationTime - 0.1),
                            &P2PGossipNetworkSimulation::StopAllNodes,
                            this);

        NS_LOG_INFO("Starting gossip network simulation for " << simulationTime << " seconds");
        Simulator::Stop(Seconds(simulationTime));
        Simulator::Run();
        Simulator::Destroy();
    }

    // closes all the connections.
    void StopAllNodes()
    {
        for (auto& node : p2pNodes)
        {
            node->Stop();
        }
        NS_LOG_INFO("All nodes stopped.");
    }

    // Prints periodic statistics during simulation
    void PrintPeriodicStats()
    {
        double simTime = Simulator::Now().GetSeconds();
        NS_LOG_INFO("=== Periodic Stats at " << simTime << "s ===");

        uint32_t totalShares = 0;
        uint32_t totalGenerated = 0;
        uint32_t totalSocketConnections = 0;

        for (const auto& node : p2pNodes)
        {
            totalShares += node->GetProcessedSharesCount();
            totalGenerated += node->GetSharesGenerated();
            totalSocketConnections += node->GetPeerSocketsCount();
        }

        NS_LOG_INFO("Total shares generated: " << totalGenerated);
        NS_LOG_INFO("Average shares per node: " << (totalShares / p2pNodes.size()));
        NS_LOG_INFO("Total socket connections: " << totalSocketConnections);
    }

    // Prints final statistics at the end of the simulation
    void PrintStatistics()
    {
        NS_LOG_INFO("=== P2P Gossip Network Simulation Statistics ===");

        uint32_t totalSharesReceived = 0;
        uint32_t totalSharesGenerated = 0;
        uint32_t totalSharesForwarded = 0;
        uint32_t totalSharesSent = 0;
        uint32_t totalSocketConnections = 0;

        for (const auto& node : p2pNodes)
        {
            totalSharesReceived += node->GetSharesReceived();
            totalSharesGenerated += node->GetSharesGenerated();
            totalSharesForwarded += node->GetSharesForwarded();
            totalSharesSent += node->GetSharesSent();
            totalSocketConnections += node->GetPeerSocketsCount();

            NS_LOG_INFO("Node " << node->GetId() << ": Generated " << node->GetSharesGenerated()
                                << ", Received " << node->GetSharesReceived() << ", Forwarded "
                                << node->GetSharesForwarded() << ", Total sent "
                                << node->GetSharesSent() << ", Total processed "
                                << node->GetProcessedSharesCount() << ", Peer count "
                                << node->GetPeers().size() << ", Socket connections "
                                << node->GetPeerSocketsCount());
        }

        NS_LOG_INFO("Total shares generated: " << totalSharesGenerated);
        NS_LOG_INFO("Total shares received: " << totalSharesReceived);
        NS_LOG_INFO("Total shares forwarded: " << totalSharesForwarded);
        NS_LOG_INFO("Total shares sent: " << totalSharesSent);
        NS_LOG_INFO("Total socket connections: " << totalSocketConnections);
    }
};

// Entry point for the simulation program
int main(int argc, char* argv[])
    {
        LogComponentEnable("P2PGossipNetworkSimulation", LOG_LEVEL_INFO);
        LogComponentEnable("P2PNode", LOG_LEVEL_INFO);

        uint32_t numNodes = 10;
        double connectionProbability = 0.3;
        double simulationTime = 60.0;
        double LatencyMs = 5.0;

        CommandLine cmd;
        cmd.AddValue("numNodes", "Number of nodes", numNodes);
        cmd.AddValue("connectionProb",
                        "Probability of connection between nodes",
                        connectionProbability);
        cmd.AddValue("simTime", "Simulation time in seconds", simulationTime);
        cmd.AddValue("Latency", "latency in ms", LatencyMs);
        cmd.Parse(argc, argv);

        P2PGossipNetworkSimulation sim(numNodes);
        sim.CreateRandomTopology(connectionProbability, LatencyMs);
        sim.Start(simulationTime);

        return 0;
    }