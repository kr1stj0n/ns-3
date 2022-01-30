/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 NITK Surathkal
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
 * Authors: Shravya Ks <shravya.ks0@gmail.com>
 *          Smriti Murali <m.smriti.95@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 */

/*
 * PORT NOTE: This code was ported from ns-2.36rc1 (queue/pie.cc).
 * Most of the comments are also ported from the same.
 */

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "shq-queue-disc.h"
#include "ns3/drop-tail-queue.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ShqQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (ShqQueueDisc);

TypeId ShqQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ShqQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<ShqQueueDisc> ()
    .AddAttribute ("MeanPktSize",
                   "Average of packet size",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&ShqQueueDisc::m_meanPktSize),
                   MakeUintegerChecker<uint32_t> ()) 
    .AddAttribute ("Alpha",
                   "Value of alpha",
                   DoubleValue (0.25),
                   MakeDoubleAccessor (&ShqQueueDisc::m_alpha),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Tinterval",
                   "Time period to calculate drop probability",
                   TimeValue (MilliSeconds (15)),
                   MakeTimeAccessor (&ShqQueueDisc::m_tInterval),
                   MakeTimeChecker ())
    .AddAttribute ("Supdate",
                   "Start time of the update timer",
                   TimeValue (Seconds (0)),
                   MakeTimeAccessor (&ShqQueueDisc::m_sUpdate),
                   MakeTimeChecker ())
    .AddAttribute ("MaxSize",
                   "The maximum number of packets accepted by this queue disc",
                   QueueSizeValue (QueueSize ("100p")),
                   MakeQueueSizeAccessor (&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                   MakeQueueSizeChecker ())
    .AddAttribute ("MaxP",
                   "Value of Maximum Probability",
                   DoubleValue (0.9),
                   MakeDoubleAccessor (&ShqQueueDisc::m_maxP),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("LinkBandwidth", 
                   "The ShQ link bandwidth",
                   DataRateValue (DataRate ("100Mbps")),
                   MakeDataRateAccessor (&ShqQueueDisc::m_linkBandwidth),
                   MakeDataRateChecker ())
    .AddAttribute ("UseEcn",
                   "True to use ECN (packets are marked instead of being dropped)",
                   BooleanValue (true),
                   MakeBooleanAccessor (&ShqQueueDisc::m_useEcn),
                   MakeBooleanChecker ())
  ;

  return tid;
}

ShqQueueDisc::ShqQueueDisc ()
  : QueueDisc (QueueDiscSizePolicy::SINGLE_INTERNAL_QUEUE)
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
  m_rtrsEvent = Simulator::Schedule (m_sUpdate, &ShqQueueDisc::CalculateProb, this);
}

ShqQueueDisc::~ShqQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
ShqQueueDisc::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_uv = 0;
  m_rtrsEvent.Cancel ();
  QueueDisc::DoDispose ();
}

Time
ShqQueueDisc::GetQueueDelay (void)
{
  return m_qDelay;
}

bool
ShqQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  QueueSize nQueued = GetCurrentSize ();

  if (nQueued + item > GetMaxSize ())
    {
      // Drops due to queue limit: reactive
      DropBeforeEnqueue (item, FORCED_DROP);
      return false;
    }

  m_countBytes += item->GetSize ();
  if (ShouldMark ())
    {
      if (!m_useEcn || !Mark (item, UNFORCED_MARK))
        {
          // Early probability drop: proactive
          DropBeforeEnqueue (item, UNFORCED_DROP);
          return false;
        }
    }

  // No drop
  bool retval = GetInternalQueue (0)->Enqueue (item);

  // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
  // internal queue because QueueDisc::AddInternalQueue sets the trace callback

  NS_LOG_LOGIC ("\t bytesInQueue  " << GetInternalQueue (0)->GetNBytes ());
  NS_LOG_LOGIC ("\t packetsInQueue  " << GetInternalQueue (0)->GetNPackets ());

  return retval;
}

void
ShqQueueDisc::InitializeParams (void)
{
  // Initially queue is empty so variables are initialize to zero
  m_countBytes = 0;
  m_qAvg = 0.0;
  m_qCur = 0.0;
  m_markProb = 0.0;
  m_maxBytes = (m_linkBandwidth.GetBitRate () / 8.0 ) * m_tInterval.GetSeconds();
}


// Check if packet p needs to be marked
bool
ShqQueueDisc::ShouldMark (void)
{
  NS_LOG_FUNCTION (this);

  double u = m_uv->GetValue ();

  if (u < m_markProb)
    {
      NS_LOG_LOGIC ("u < m_markProb; u " << u << "; m_markProb " << m_markProb);

      return true; // mark
    }

  return false; // no mark
}

void ShqQueueDisc::CalculateProb ()
{
  NS_LOG_FUNCTION (this);
  uint32_t nQueued = 0;
  double p = 0.0;

  NS_LOG_DEBUG ("It's time to calculate the probability");

  // current queue size in packets / bytes (backlog)
  nQueued = GetInternalQueue (0)->GetCurrentSize ().GetValue ();
  m_countBytes += nQueued * m_meanPktSize;
  
  m_qAvg = m_qAvg * (1.0 - m_alpha) + (m_countBytes * m_alpha);
  p = m_maxP * m_qAvg / m_maxBytes;

  // Update values
  m_markProb = p;
  m_countBytes = 0;
  
  m_rtrsEvent = Simulator::Schedule (m_tInterval, &ShqQueueDisc::CalculateProb, this);
}

Ptr<QueueDiscItem>
ShqQueueDisc::DoDequeue ()
{
  NS_LOG_FUNCTION (this);

  if (GetInternalQueue (0)->IsEmpty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();
  NS_ASSERT_MSG (item != nullptr, "Dequeue null, but internal queue not empty");

  m_qDelay = Now () - item->GetTimeStamp ();

  if (GetInternalQueue (0)->GetNBytes () == 0)
    {
      m_qDelay = Seconds (0);
    }

  return item;
}

bool
ShqQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("ShqQueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () > 0)
    {
      NS_LOG_ERROR ("ShqQueueDisc cannot have packet filters");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      // add  a DropTail queue
      AddInternalQueue (CreateObjectWithAttributes<DropTailQueue<QueueDiscItem> >
                          ("MaxSize", QueueSizeValue (GetMaxSize ())));
    }

  if (GetNInternalQueues () != 1)
    {
      NS_LOG_ERROR ("ShqQueueDisc needs 1 internal queue");
      return false;
    }

  return true;
}

} //namespace ns3
