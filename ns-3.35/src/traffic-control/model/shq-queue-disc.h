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
 * PORT NOTE: This code was ported from ns-2.36rc1 (queue/pie.h).
 * Most of the comments are also ported from the same.
 */

#ifndef SHQ_QUEUE_DISC_H
#define SHQ_QUEUE_DISC_H

#include "ns3/queue-disc.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/timer.h"
#include "ns3/event-id.h"
#include "ns3/random-variable-stream.h"

class ShqQueueDiscTestCase;  // Forward declaration for unit test
namespace ns3 {

class TraceContainer;
class UniformRandomVariable;

/**
 * \ingroup traffic-control
 *
 * \brief Implements ShQ Active Queue Management discipline
 */
class ShqQueueDisc : public QueueDisc
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief ShqQueueDisc Constructor
   */
  ShqQueueDisc ();

  /**
   * \brief ShqQueueDisc Destructor
   */
  virtual ~ShqQueueDisc ();

  /**
   * \brief Get queue delay.
   *
   * \returns The current queue delay.
   */
  Time GetQueueDelay (void);

  // Reasons for dropping packets
  static constexpr const char* FORCED_DROP = "Forced drop";      //!< Drops due to queue limit: reactive
  static constexpr const char* UNFORCED_DROP = "Unorced drop";   //!< Drops due to ECN errors: proactive
  static constexpr const char* UNFORCED_MARK = "Unforced mark";  //!< Early probability marks: proactive

protected:
  /**
   * \brief Dispose of the object
   */
  virtual void DoDispose (void);

private:
  friend class::ShqQueueDiscTestCase;         // Test code
  virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
  virtual Ptr<QueueDiscItem> DoDequeue (void);
  virtual bool CheckConfig (void);

  /**
   * \brief Initialize the queue parameters.
   */
  virtual void InitializeParams (void);

  /**
   * \brief Check if a packet needs to be dropped due to probability drop
   * \param item queue item
   * \param qSize queue size
   * \returns 0 for no drop, 1 for drop
   */
  bool ShouldMark (void);

  /**
   * Periodically update the drop probability based on the delay samples:
   * not only the current delay sample but also the trend where the delay
   * is going, up or down
   */
  void CalculateProb (void);


  // ** Variables supplied by user
  Time		m_sUpdate;		//!< Start time of the update timer
  Time		m_tInterval;		//!< Time period after which CalculateProb () is called
  uint32_t	m_meanPktSize;		//!< Average packet size in bytes
  double	m_maxP;			//!< The max probability of marking a packet
  double	m_alpha;		//!< Parameter to shq controller
  DataRate	m_linkBandwidth;	//!< Link bandwidth
  bool		m_useEcn;		//!< Enable ECN Marking functionality

  // ** Variables maintained by ShQ
  uint32_t	m_countBytes;		//!< Number of bytes since last prob calculation
  double	m_qAvg;			//!< Average queue length
  double	m_qCur;			//!< Current queue length
  double	m_markProb;		//!< Variable used in calculation of mark probability
  Time		m_qDelay;		//!< Current value of queue delay
  uint32_t	m_maxBytes;		//!< Number of bytes that can be enqueued during m_tInterval
  EventId	m_rtrsEvent;		//!< Event used to decide the decision of interval of mark probability calculation
  Ptr<UniformRandomVariable> m_uv;	//!< Rng stream
};

};   // namespace ns3

#endif

