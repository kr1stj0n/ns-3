/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017-20 NITK Surathkal
 * Copyright (c) 2020 Tom Henderson (better alignment with experiment)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Shravya K.S. <shravya.ks0@gmail.com>
 *          Apoorva Bhargava <apoorvabhargava13@gmail.com>
 *          Shikha Bakshi <shikhabakshi912@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 *          Tom Henderson <tomh@tomh.org>
 */

// The network topology used in this example is based on Fig. 17 described in
// Mohammad Alizadeh, Albert Greenberg, David A. Maltz, Jitendra Padhye,
// Parveen Patel, Balaji Prabhakar, Sudipta Sengupta, and Murari Sridharan.
// "Data Center TCP (DCTCP)." In ACM SIGCOMM Computer Communication Review,
// Vol. 40, No. 4, pp. 63-74. ACM, 2010.

// The topology is roughly as follows
//
//
//   S -----1Gbps----- T1 ---100Mbps--- T2 -----1Gbps----- R
//
//
//
// The link between switch T1 and T2 is 10 Gbps.  All other
// links are 1 Gbps.  In the SIGCOMM paper, there is a Scorpion switch
// between T1 and T2, but it doesn't contribute another bottleneck.
//
// S1 and S3 each have 10 senders sending to receiver R1 (20 total)
// S2 (20 senders) sends traffic to R2 (20 receivers)
//
// This sets up two bottlenecks: 1) T1 -> T2 interface (30 senders
// using the 10 Gbps link) and 2) T2 -> R1 (20 senders using 1 Gbps link)
//
// RED queues configured for ECN marking are used at the bottlenecks.
//
// Figure 17 published results are that each sender in S1 gets 46 Mbps
// and each in S3 gets 54 Mbps, while each S2 sender gets 475 Mbps, and
// that these are within 10% of their fair-share throughputs (Jain index
// of 0.99).
//
// This program runs the program by default for five seconds.  The first
// second is devoted to flow startup (all 40 TCP flows are stagger started
// during this period).  There is a three second convergence time where
// no measurement data is taken, and then there is a one second measurement
// interval to gather raw throughput for each flow.  These time intervals
// can be changed at the command line.
//
// The program outputs six files.  The first three:
// * dctcp-example-s1-r1-throughput.dat
// * dctcp-example-s2-r2-throughput.dat
// * dctcp-example-s3-r1-throughput.dat
// provide per-flow throughputs (in Mb/s) for each of the forty flows, summed
// over the measurement window.  The fourth file,
// * dctcp-example-fairness.dat
// provides average throughputs for the three flow paths, and computes
// Jain's fairness index for each flow group (i.e. across each group of
// 10, 20, and 10 flows).  It also sums the throughputs across each bottleneck.
// The fifth and sixth:
// * dctcp-example-t1-length.dat
// * dctcp-example-t2-length.dat
// report on the bottleneck queue length (in packets and microseconds
// of delay) at 10 ms intervals during the measurement window.
//
// By default, the throughput averages are 23 Mbps for S1 senders, 471 Mbps
// for S2 senders, and 74 Mbps for S3 senders, and the Jain index is greater
// than 0.99 for each group of flows.  The average queue delay is about 1ms
// for the T2->R2 bottleneck, and about 200us for the T1->T2 bottleneck.
//
// The RED parameters (min_th and max_th) are set to the same values as
// reported in the paper, but we observed that throughput distributions
// and queue delays are very sensitive to these parameters, as was also
// observed in the paper; it is likely that the paper's throughput results
// could be achieved by further tuning of the RED parameters.  However,
// the default results show that DCTCP is able to achieve high link
// utilization and low queueing delay and fairness across competing flows
// sharing the same path.

#include <iostream>
#include <iomanip>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

std::stringstream filePlotQueue1;
std::ofstream rxSRThroughput;
std::ofstream fairnessIndex;
std::ofstream t1QueueLength;
std::vector<uint64_t> rxSRBytes;
std::vector<uint64_t> rxSRBytesInterval;

void
PrintProgress (Time interval)
{
  std::cout << "Progress to " << std::fixed << std::setprecision (1) << Simulator::Now ().GetSeconds () << " seconds simulation time" << std::endl;
  Simulator::Schedule (interval, &PrintProgress, interval);
}

void
TraceSRSink (std::size_t index, Ptr<const Packet> p, const Address& a)
{
  rxSRBytes[index] += p->GetSize ();
  rxSRBytesInterval[index] += p->GetSize ();
}

void
InitializeCounters (void)
{
  for (std::size_t i = 0; i < 1; i++)
    {
      rxSRBytes[i] = 0;
      rxSRBytesInterval[i] = 0;
    }
}

void
PrintThroughput (Time measurementWindow)
{
  for (std::size_t i = 0; i < 1; i++)
    {
      rxSRThroughput << Simulator::Now ().GetSeconds () << "s " << i << " "
                     << (rxSRBytesInterval[i] * 8) / (measurementWindow.GetSeconds ()) / 1e6
                     << "Mbps" << std::endl;
      rxSRBytesInterval[i] = 0;
      Simulator::Schedule (MilliSeconds (1000), &PrintThroughput, measurementWindow);
    }
}

// Jain's fairness index:  https://en.wikipedia.org/wiki/Fairness_measure
void
PrintFairness (Time measurementWindow)
{
  double average = 0;
  uint64_t sumSquares = 0;
  uint64_t sum = 0;
  double fairness = 0;
  for (std::size_t i = 0; i < 1; i++)
    {
      sum += rxSRBytes[i];
      sumSquares += (rxSRBytes[i] * rxSRBytes[i]);
    }
  average = ((sum / 1) * 8 / measurementWindow.GetSeconds ()) / 1e6;
  fairness = static_cast<double> (sum * sum) / (1 * sumSquares);
  fairnessIndex << "Average throughput for S-R flows: "
                << std::fixed << std::setprecision (2) << average << " Mbps; fairness: "
                << std::fixed << std::setprecision (3) << fairness << std::endl;
  sum = 0;
  for (std::size_t i = 0; i < 1; i++)
    {
      sum += rxSRBytes[i];
    }
  fairnessIndex << "Aggregate user-level throughput for flows through T1: "
                << static_cast<double> (sum * 8) / measurementWindow.GetSeconds () / 1e6
                << " Mbps" << std::endl;
}

void
CheckT1QueueSize (Ptr<QueueDisc> queue)
{
  // 1500 byte packets
  uint32_t qSize = queue->GetNPackets ();
  Time backlog = Seconds (static_cast<double> (qSize * 1500 * 8) / 1e6); // 1 Mb/s
  // report size in units of packets and ms
  t1QueueLength << std::fixed << std::setprecision (2) << Simulator::Now ().GetSeconds () << " " << qSize << " " << backlog.GetMicroSeconds () << std::endl;
  // check queue size every 1/100 of a second
  Simulator::Schedule (MilliSeconds (1), &CheckT1QueueSize, queue);
}


int main (int argc, char *argv[])
{
  std::string outputFilePath = ".";
  std::string tcpTypeId = "TcpLinuxReno";
  Time flowStartupWindow = Seconds (1);
  Time convergenceTime = Seconds (8);
  Time measurementWindow = Seconds (1);
  bool enableSwitchEcn = true;
  Time progressInterval = MilliSeconds (100);
  bool isPcapEnabled = true;
  std::string pcapFileName = "dctcp-pcapFile.pcap";

  CommandLine cmd (__FILE__);
  cmd.AddValue ("tcpTypeId", "ns-3 TCP TypeId", tcpTypeId);
  cmd.AddValue ("flowStartupWindow", "startup time window (TCP staggered starts)", flowStartupWindow);
  cmd.AddValue ("convergenceTime", "convergence time", convergenceTime);
  cmd.AddValue ("measurementWindow", "measurement window", measurementWindow);
  cmd.AddValue ("enableSwitchEcn", "enable ECN at switches", enableSwitchEcn);
  cmd.Parse (argc, argv);

  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + tcpTypeId));

  Time startTime = Seconds (0);
  Time stopTime = flowStartupWindow + convergenceTime + measurementWindow;

  Time clientStartTime = startTime;

  rxSRBytes.reserve (1);
  rxSRBytesInterval.reserve (1);
  
  Ptr<Node> S = CreateObject<Node> ();
  Ptr<Node> T1 = CreateObject<Node> ();
  Ptr<Node> T2 = CreateObject<Node> ();
  Ptr<Node> R = CreateObject<Node> ();

  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1448));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (2));

  // Enable/Disable checksum
  if (isPcapEnabled)
    {
      GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    }
  else
    {
      GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));
    }

  // Set default parameters for RED queue disc
  Config::SetDefault ("ns3::RedQueueDisc::UseEcn", BooleanValue (enableSwitchEcn));
  Config::SetDefault ("ns3::RedQueueDisc::UseHardDrop", BooleanValue (false));
  Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (1500));
  // Triumph and Scorpion switches used in DCTCP Paper have 4 MB of buffer
  // If every packet is 1500 bytes, 2666 packets can be stored in 4 MB
  Config::SetDefault ("ns3::RedQueueDisc::MaxSize", QueueSizeValue (QueueSize ("1000p")));
  // DCTCP tracks instantaneous queue length only; so set QW = 1
  Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (1));
  Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (1));
  Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (2));

  PointToPointHelper pointToPointSR;
  pointToPointSR.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  pointToPointSR.SetChannelAttribute ("Delay", StringValue ("1ms"));

  PointToPointHelper pointToPointT;
  pointToPointT.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  pointToPointT.SetChannelAttribute ("Delay", StringValue ("5ms"));


  // Create a total of 3 links.
  NetDeviceContainer ST1 = pointToPointSR.Install (S, T1);
  NetDeviceContainer T1T2 = pointToPointT.Install (T1, T2);
  NetDeviceContainer RT2 = pointToPointSR.Install (R, T2);


  InternetStackHelper stack;
  stack.InstallAll ();

  TrafficControlHelper tchRed;
  // MinTh = 5, MaxTh = 15 recommended in ACM SIGCOMM 2010 DCTCP Paper
  // This yields a target (MinTh) queue depth of 60us at 10 Gb/s
  tchRed.SetRootQueueDisc ("ns3::RedQueueDisc",
                           "LinkBandwidth", StringValue ("10Mbps"),
                           "LinkDelay", StringValue ("5ms"),
                           "MinTh", DoubleValue (1),
                           "MaxTh", DoubleValue (2));
  // Install the queue only at T1
  QueueDiscContainer redQueueDisc = tchRed.Install (T1T2.Get (0));

  // Bottleneck link traffic control configuration
  TrafficControlHelper tchPfifo;
  tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc", "MaxSize",
                             StringValue ("1000p"));

  tchPfifo.Install (ST1);
  tchPfifo.Install (RT2);
  tchPfifo.Install (T1T2.Get (1));

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ipST1 = address.Assign (ST1);
  address.SetBase ("10.2.2.0", "255.255.255.0");
  Ipv4InterfaceContainer ipT1T2 = address.Assign (T1T2);
  address.SetBase ("10.3.3.0", "255.255.255.0");
  Ipv4InterfaceContainer ipRT2 = address.Assign (RT2);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


  // Sender S sends to receiver R
  std::vector<Ptr<PacketSink> > rSinks;
  rSinks.reserve (1);
  for (std::size_t i = 0; i < 1; i++)
    {
      uint16_t port = 50000 + i;
      Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
      ApplicationContainer sinkApp = sinkHelper.Install (R);
      Ptr<PacketSink> packetSink = sinkApp.Get (0)->GetObject<PacketSink> ();
      rSinks.push_back (packetSink);
      sinkApp.Start (startTime);
      sinkApp.Stop (stopTime);

      OnOffHelper clientHelper1 ("ns3::TcpSocketFactory", Address ());
      clientHelper1.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
      clientHelper1.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      clientHelper1.SetAttribute ("DataRate", DataRateValue (DataRate ("1Gbps")));
      clientHelper1.SetAttribute ("PacketSize", UintegerValue (1448));

      ApplicationContainer clientApps1;
      AddressValue remoteAddress (InetSocketAddress (ipRT2.GetAddress (0), port));
      clientHelper1.SetAttribute ("Remote", remoteAddress);
      clientApps1.Add (clientHelper1.Install (S));
      clientApps1.Start (i * flowStartupWindow / 1  + clientStartTime + MilliSeconds (i * 1));
      clientApps1.Stop (stopTime);
    }

  rxSRThroughput.open ("dctcp-example-s-r-throughput.dat", std::ios::out);
  rxSRThroughput << "#Time(s) flow thruput(Mb/s)" << std::endl;
  fairnessIndex.open ("dctcp-example-fairness.dat", std::ios::out);
  t1QueueLength.open ("dctcp-example-t1-length.dat", std::ios::out);
  t1QueueLength << "#Time(s) qlen(pkts) qdelay(us)" << std::endl;
  for (std::size_t i = 0; i < 1; i++)
    {
      rSinks[i]->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&TraceSRSink, i));
    }
  Simulator::Schedule (flowStartupWindow, &InitializeCounters);
  Simulator::Schedule (flowStartupWindow, &PrintThroughput, measurementWindow);
  Simulator::Schedule (flowStartupWindow + convergenceTime + measurementWindow,
                       &PrintFairness, flowStartupWindow + convergenceTime + measurementWindow);
  Simulator::Schedule (progressInterval, &PrintProgress, progressInterval);
  Simulator::Schedule (flowStartupWindow, &CheckT1QueueSize, redQueueDisc.Get (0));
  Simulator::Stop (stopTime + TimeStep (1));

  if (isPcapEnabled)
    {
      pointToPointSR.EnablePcap (pcapFileName, ST1.Get (0), true);
    }

  Simulator::Run ();

  rxSRThroughput.close ();
  fairnessIndex.close ();
  t1QueueLength.close ();
  Simulator::Destroy ();
  return 0;
}
