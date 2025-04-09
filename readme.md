# P2P Gossip Network Simulation

A network simulation built with NS-3 that demonstrates peer-to-peer gossip protocol behavior in distributed networks.

## Overview

This simulation models a peer-to-peer network where nodes communicate using a gossip protocol. Each node periodically generates "shares" (messages) and propagates them to its connected peers. The simulation visualizes network topology, message propagation, and collects statistics about the network's behavior.

## Features

- Random network topology generation with configurable connection probability
- Configurable network parameters (latency, data rate)
- Visualization of network topology and message propagation using NetAnim
- Comprehensive statistics collection and reporting
- TCP-based communication between nodes

## Requirements

- NS-3 (Network Simulator 3)
- C++ compiler with C++11 support
- NetAnim for visualization

## Building and Running

1. Make sure NS-3 is installed on your system
2. Copy the simulation code to your NS-3 scratch directory:
   ```bash
   cp p2p-gossip-network.cc ~/ns-3-dev/scratch/
   ```
3. Navigate to your NS-3 directory and run the simulation:
   ```bash
   cd ~/ns-3-dev
   ./ns3 run p2p-gossip-network.cc
   ```


## Visualization

The simulation generates an XML file (`p2p-gossip-tcp-animation.xml`) that can be loaded into NetAnim for visualization. The visualization shows:

- Network topology with nodes and connections
- Color-coded nodes based on their connectivity degree:
  - Blue: Low degree (≤ 2 connections)
  - Green: Medium degree (3-4 connections)
  - Red: High degree (> 4 connections)
- Message propagation between nodes
- Node statistics and information

### Running the Visualization

1. After running the simulation, open NetAnim:
   ```bash
   cd ~/ns-3-dev/netanim
   ./NetAnim
   ```

2. In NetAnim, click on "File" > "Open Animation" and select the generated `p2p-gossip-tcp-animation.xml` file.

3. Use the NetAnim controls to play, pause, and step through the simulation.

## Demo Visualization

[📽️ Demo Video](demo.webm)


## Understanding the Code

### Key Classes

- `P2PNode`: Represents a node in the network that can generate and relay shares
- `Share`: Data structure representing a message passed between nodes
- `P2PGossipNetworkSimulation`: Main simulation class that sets up the network and runs the simulation

### Simulation Process

1. Nodes are created and connected based on specified probability
2. Each node starts generating shares at random intervals
3. When a node receives a share, it processes it and forwards it to its peers
4. The simulation collects statistics about share propagation and network behavior
5. Results are displayed at the end of the simulation

## Collected Statistics

The simulation collects and reports the following statistics:

- Total messages sent across the network
- Number of shares generated, received, and forwarded by each node
- Total shares processed by each node
- Peer count for each node

