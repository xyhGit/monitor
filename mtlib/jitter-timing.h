//
//  jitter-timing.h
//  mtndn
//
//  Created by Peter Gusev on 5/8/14.
//  Copyright 2013-2015 Regents of the University of California
//

#ifndef _jitter_timing_
#define _jitter_timing_

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/recursive_mutex.hpp>

#include "mtndn-object.h"
#include "frame-data.h"


/**
  *Video jitter buffer timing class
  *Provides interface for managing playout timing in separate playout thread
  *Playout thread itratively calls function which extracts frames from the
  *jitter buffer, renders them and sets a timer for the frame playout delay,
  *which is calculated from the timestamps, provided by producer and
  *adjusted by this class in order to accomodate processing delays
  *(extracting frame from the jitter buffer, rendering frame on the canvas,
  *etc.).
 */
class JitterTiming : public MtNdnComponent
{
public:
    JitterTiming();
    ~JitterTiming();

    void flush();
    void stop();

    /**
      *Should be called in the beginning of the each playout iteration
      *@return current time in microseconds
     */
    int64_t startFramePlayout();

    /**
      *Should be called whenever playout time (as provided by producer) is
      *known by the consumer. Usually, jitter buffer provides this value as
      *a result of releaseAcqiuredFrame call.
      *@param framePlayoutTime Playout time meant by producer (difference
      *                        between conqequent frame's timestamps)
     */
    void updatePlayoutTime(int framePlayoutTime, PacketNumber packetNo);

    /**
      *Sets up playback timer asynchronously.
      *@param callback A callback to call when timer is fired
     */
    void run(boost::function<void()> callback);

private:
    int framePlayoutTimeMs_ = 0;
    int processingTimeUsec_ = 0;
    int64_t prevPlayoutTsUsec_ = 0;

    void resetData();
};


#endif /*defined(__mtndn__jitter_timing__) */
