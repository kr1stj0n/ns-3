/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
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
 * Author: Shravya K.S. <shravya.ks0@gmail.com>
 *
 */

#include "tcp-dctcp.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpLgc");

NS_OBJECT_ENSURE_REGISTERED (TcpLgc);

TypeId TcpLgc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpLgc")
    .SetParent<TcpLinuxReno> ()
    .AddConstructor<TcpLgc> ()
    .SetGroupName ("Internet")
    .AddAttribute ("LgcPhi",
                   "Parameter Phi ~2.78",
                   DoubleValue (2.78),
                   MakeDoubleAccessor (&TcpLgc::m_LgcPhi),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("LgcAlpha",
                   "Parameter Alpha ~0.25",
                   DoubleValue (0.25),
                   MakeDoubleAccessor (&TcpLgc::m_LgcAlpha),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("LgcLogP",
                   "Parameter LogP ~1.4",
                   DoubleValue (1.4),
                   MakeDoubleAccessor (&TcpLgc::m_LgcLogP),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("LgcCoef",
                   "Parameter Coef ~20",
                   UintegerValue (20),
                   MakeUintegerAccessor (&TcpLgc::m_LgcCoef),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxDataRate", 
                   "Parameter MaxDataRate ~100Mbps",
                   DataRateValue (DataRate (100Mbps"")),
                   MakeDataRateAccessor (&TcpLgc::m_LgcMaxDataRate),
                   MakeDataRateChecker ())
    .AddTraceSource ("CongestionEstimate",
                     "Update sender-side congestion estimate state",
                     MakeTraceSourceAccessor (&TcpLgc::m_traceCongestionEstimate),
                     "ns3::TcpLgc::CongestionEstimateTracedCallback")
  ;
  return tid;
}

std::string TcpLgc::GetName () const
{
  return "TcpLgc";
}

TcpLgc::TcpLgc ()
  : TcpLinuxReno (),
    m_ackedBytesEcn (0),
    m_ackedBytesTotal (0),
    m_priorRcvNxt (SequenceNumber32 (0)),
    m_priorRcvNxtFlag (false),
    m_nextSeq (SequenceNumber32 (0)),
    m_nextSeqFlag (false),
    m_ceState (false),
    m_delayedAckReserved (false),
    m_initialized (false)
{
  NS_LOG_FUNCTION (this);
}

TcpLgc::TcpLgc (const TcpLgc& sock)
  : TcpLinuxReno (sock),
    m_ackedBytesEcn (sock.m_ackedBytesEcn),
    m_ackedBytesTotal (sock.m_ackedBytesTotal),
    m_priorRcvNxt (sock.m_priorRcvNxt),
    m_priorRcvNxtFlag (sock.m_priorRcvNxtFlag),
    m_alpha (sock.m_alpha),
    m_nextSeq (sock.m_nextSeq),
    m_nextSeqFlag (sock.m_nextSeqFlag),
    m_ceState (sock.m_ceState),
    m_delayedAckReserved (sock.m_delayedAckReserved),
    m_g (sock.m_g),
    m_useEct0 (sock.m_useEct0),
    m_initialized (sock.m_initialized)
{
  NS_LOG_FUNCTION (this);
}

TcpLgc::~TcpLgc (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps> TcpLgc::Fork (void)
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpLgc> (this);
}

void
TcpDctcp::Init (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_INFO (this << "Enabling DctcpEcn for DCTCP");
  tcb->m_useEcn = TcpSocketState::On;
  tcb->m_ecnMode = TcpSocketState::DctcpEcn;
  tcb->m_ectCodePoint = m_useEct0 ? TcpSocketState::Ect0 : TcpSocketState::Ect1;
  m_initialized = true;
}

// Step 9, Section 3.3 of RFC 8257.  GetSsThresh() is called upon
// entering the CWR state, and then later, when CWR is exited,
// cwnd is set to ssthresh (this value).  bytesInFlight is ignored.
uint32_t
TcpDctcp::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  return static_cast<uint32_t> ((1 - m_alpha / 2.0) * tcb->m_cWnd);
}

void
TcpDctcp::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time &rtt)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);
  m_ackedBytesTotal += segmentsAcked * tcb->m_segmentSize;
  if (tcb->m_ecnState == TcpSocketState::ECN_ECE_RCVD)
    {
      m_ackedBytesEcn += segmentsAcked * tcb->m_segmentSize;
    }
  if (m_nextSeqFlag == false)
    {
      m_nextSeq = tcb->m_nextTxSequence;
      m_nextSeqFlag = true;
    }
  if (tcb->m_lastAckedSeq >= m_nextSeq)
    {
      double bytesEcn = 0.0; // Corresponds to variable M in RFC 8257
      if (m_ackedBytesTotal >  0)
        {
          bytesEcn = static_cast<double> (m_ackedBytesEcn * 1.0 / m_ackedBytesTotal);
        }
      m_alpha = (1.0 - m_g) * m_alpha + m_g * bytesEcn;
      m_traceCongestionEstimate (m_ackedBytesEcn, m_ackedBytesTotal, m_alpha);
      NS_LOG_INFO (this << "bytesEcn " << bytesEcn << ", m_alpha " << m_alpha);
      Reset (tcb);
    }
}

void
TcpLgc::InitializeDctcpAlpha (double alpha)
{
  NS_LOG_FUNCTION (this << alpha);
  NS_ABORT_MSG_IF (m_initialized, "DCTCP has already been initialized");
  m_alpha = alpha;
}

void
TcpLgc::Reset (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_nextSeq = tcb->m_nextTxSequence;
  m_ackedBytesEcn = 0;
  m_ackedBytesTotal = 0;
}

} // namespace ns3
