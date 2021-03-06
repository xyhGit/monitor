/*
  *frame-buffer.h
 *
  * Created on: Jun 15, 2016
  *     Author: xyh
 */

#ifndef FRAME_BUFFER_H_
#define FRAME_BUFFER_H_

#include <iostream>
#include <functional>
#include <thread>
#include <mutex>

#include <queue>
#include <vector>
#include <map>
#include <list>

#include <ndn-cpp/name.hpp>
#include <ndn-cpp/data.hpp>

#include "frame-data.h"
#include "mtndn-object.h"
#include "statistics.h"
#include "simple-log.h"
#include <boost/thread/shared_mutex.hpp>
using namespace std;
using namespace ndn;


class FrameBuffer : public MtNdnComponent//ndnlog::new_api::ILoggingObject
{
public:

    enum State{
        Invalid = 0,
        Valid = 1
    };

    class Slot
    {
    public:

        typedef enum{

            StateNotUsed = 0,    // frame is no used
            StatePending = 1,    // frame awaits it's interest to be answered
            StateMissing = 2,    // frame was timed out
            StateFetched = 3,    // frame has been fetched already
            StateLocked  = 4     // slot is locked for decoding



        }State;

        class Comparator
        {
        public:
            Comparator(bool inverted = false):inverted_(inverted){}

            bool operator() (const ptr_lib::shared_ptr<Slot> slot1, const ptr_lib::shared_ptr<Slot> slot2)
            {
                return slot1->getFrameNo() > slot2->getFrameNo();
            }
/*
            bool operator < (Slot slot1, Slot slot2)
            {
                return slot1.slotNumber_ < slot2.slotNumber_;
            }
*/

        private:
            bool inverted_;
        };

        Slot();
        ~Slot();

        /**
          *Discards frame by swithcing it to NotUsed state and
          *reseting all the attributes
         */
        void
        discard();

        /**
          *Moves frame into Pending state and updaets following
          *attributes:
          *- requestTimeUsec
          *- interestName
          *- reqCounter
         */
        void
        interestIssued();

        /**
          *Moves frame into Missing state if it was in Pending
          *state before
         */
        void
        markMissed();

        /**
          *Moves frame into Fetched state and updates following
          *attributes:
          *- dataName
          *- arrivalTimeUsec
         */
        bool
        dataArrived();

        void
        setPayloadSize(unsigned int payloadSize)
        { payloadSize_ = payloadSize; }

        unsigned int
        getPayloadSize() const { return payloadSize_; }

        void
        setDataPtr(const unsigned char *dataPtr)
        { dataPtr_ = const_cast<unsigned char*>(dataPtr); }

        unsigned char*
        getDataPtr() const { return dataPtr_; }

        const ndn::ptr_lib::shared_ptr<Data>
        getNdnDataPtr() const
        { return spNdnData_; }

        void
        setNdnDataPtr(const ndn::ptr_lib::shared_ptr<Data> dataPtr)
        { spNdnData_ = dataPtr; }

        void
        setNumber(FrameNumber number)
        { frameNumber_ = number; }

        FrameNumber
        getFrameNo() const
        { return frameNumber_; }

        State
        getState() const
        { return sstate_; }

        State
        getStashedState() const
        { return stashedState_; }

        void
        setPrefix(const Name &prefix)
        { prefix_ = prefix; }

        const Name&
        getPrefix() { return prefix_; }

        void
        lock()
        {
            stashedState_ = sstate_;
            sstate_ = StateLocked;
            //syncMutex_.lock();
        }

        void
        unlock()
        {
            sstate_ = stashedState_;
            //syncMutex_.unlock();
        }


        int64_t
        getCaptureTimestampUsec()
        { return captureTimestampUsec_; }

        void
        setCaptureTimestampUsec(int64_t captureTimestampUsec)
        { captureTimestampUsec_ = captureTimestampUsec; }

        int64_t
        getRequestTimestampUsec()
        { return requestTimestampUsec_; }

        void
        setRequestTimestampUsec(int64_t requestTimestampUsec)
        { requestTimestampUsec_ = requestTimestampUsec; }

        int64_t
        getArrivalTimestampUsec()
        { return arrivalTimestampUsec_; }

        void
        setArrivalTimestampUsec(int64_t arrivalTimestampUsec)
        { arrivalTimestampUsec_ = arrivalTimestampUsec; }

        int64_t
        getRoundTripDelayUsec()
        {
            if (arrivalTimestampUsec_ <= 0 || requestTimestampUsec_ <= 0)
                return -1;
            return (arrivalTimestampUsec_-requestTimestampUsec_);
        }


    protected:

        FrameNumber frameNumber_;
        Name prefix_;

        unsigned int payloadSize_;  // size of actual data payload
                                    // (without frame header)
        unsigned char *dataPtr_;    // pointer to the payload data
        ndn::ptr_lib::shared_ptr<Data> spNdnData_;

        State sstate_, stashedState_;

        int64_t captureTimestampUsec_,  // frame captured timestamp
                requestTimestampUsec_, // local timestamp when the interest
                                  // for this frame was issued
                arrivalTimestampUsec_; // local timestamp when data for this
                                  // frame has arrived
        //std::recursive_mutex syncMutex_;

        void resetData();

    };// class Slot///////////////////////////////////////////////////////////////


    FrameBuffer():
        count_(0),
        readySlots_count_(0),
        fstate_(Invalid),
        statistic_(Statistics::getInstance())
    {
        setDescription("[FrameBuffer]\t");
        VLOG(LOG_INFO) << setw(20) << setfill(' ') << std::right << getDescription()
                       << "ctor" << std::endl;
    }


    ~FrameBuffer()
    {
        VLOG(LOG_INFO) << setw(20) << setfill(' ') << std::right << getDescription()
                       << "dtor" << std::endl;
    }

    void
    init()
    {
        reset();
        setState(Valid);
    }

    void
    stop()
    {
        setState(Invalid);
        LOG(INFO) << setw(20) << setfill(' ') << std::right << getDescription()
                  << "stoped" << endl;
    }

    State
    getState()
    { return fstate_; }

    void
    setState(const FrameBuffer::State &state)
    {
        ptr_lib::lock_guard<ptr_lib::recursive_mutex> scopedLock(syncMutex_);
        fstate_ = state;
    }


    //*********************************************************
    bool
    interestIssued( ndn::Interest &interest );

    void
    setSlot(const ndn::ptr_lib::shared_ptr<Data> &data, ptr_lib::shared_ptr<Slot> slot);

    void
    recvData(const ndn::ptr_lib::shared_ptr<Data> &data);

    bool
    dataMissed(const ptr_lib::shared_ptr<const Interest> &interest );

    /**
     * Acquires current top slot from the playback queue for playback.
     * Acquired slot will be locked unless corresponding
     * releaseAcquiredSlot method will be called. One can rely on slot's
     * data consistency between these calls. That's why passing a frame
     * into decoder should be implemented between these calls.
     * @param packetData Pointer to the packet's data, where frame's
     *                   data wil be extracted
     * @param assembledLevel Frame assembled level, i.e. ratio (0<r<1)
     *                       of how many segments were assembled. 1
     *                       means frame was fully assembled, 0 means
     *                       frame was not assembled at all (most likely
     *                       in this case, *packetData will be null).
     */
    void
    acquireFrame(vector<uint8_t> &frame, int64_t &captureTsUsec,
                 PacketNumber &packetNo,
                 PacketNumber &sequencePacketNo,
                 PacketNumber &pairedPacketNo,
                 bool &isKey, double &assembledLevel);

    /**
     * Releases previously acquired slot.
     * After this call, slot will be unlocked, reset and freed in the
     * buffer so it can be re-used for new frames. One should not rely
     * on data slot's consistency after this call.
     * @param (out) isInferredPlayback Indicates, whether the value
     * returned is an inferred playback duration (as opposed to duration
     * calculated using timestamp difference).
     * @return Playback duration (ms)
     */
    int
    releaseAcquiredFrame(bool& isInferredPlayback);

    unsigned int
    getPlayableBufferSize()
    { return readySlots_count_; }

    int64_t
    getPlayableBufferDuration()
    {
        ptr_lib::lock_guard<ptr_lib::recursive_mutex> scopedLock(syncMutex_);
        int64_t minTs=0x7fffffff, maxTs=0;
        SlotPtr slot;
        map_int_slotPtr::iterator it = activeSlots_.begin();
        if( readySlots_count_ > 1 )
            while( it != activeSlots_.end() )
            {
                slot = it->second;
                if( slot->getState() >= Slot::StateFetched )
                {
                    if(slot->getCaptureTimestampUsec() < minTs)
                        minTs = slot->getCaptureTimestampUsec();
                    if(slot->getCaptureTimestampUsec() > maxTs)
                        maxTs = slot->getCaptureTimestampUsec();
                }
                ++it;
            }
        else
            return 0;
        //VLOG(LOG_TRACE) << "duration " << maxTs << " - " << minTs << std::endl;
        return maxTs - minTs;
    }

    int64_t
    getInferredFrameDuration()
    {
        return ceil(1000./playbackRate_);
    }

    int
    getActiveSlots()
    {
        return activeSlots_.size();
    }

    int
    getReadySlots()
    {
        return readySlots_count_;
    }

    int
    getSuccessiveMiss()
    {
        return miss_count_;
    }

private:

    /*
    typedef std::vector<FrameBuffer::Slot*> PlaybackQueueBase;

    class PlaybackQueue : public PlaybackQueueBase
    {
    public:
        PlaybackQueue(double playbackRate = 30.);

        int64_t
        getPlaybackDuration(bool estimate = true);

        void
        updatePlaybackDeadlines();

        void
        pushSlot(FrameBuffer::Slot *const slot);

        FrameBuffer::Slot*
        peekSlot();

        void
        popSlot();

        void
        updatePlaybackRate(double playbackRate);

        double
        getPlaybackRate() const { return playbackRate_; }

        void
        clear();

        int64_t
        getInferredFrameDuration()
        { //return lastFrameDuration_;
            return ceil(1000./playbackRate_);
        }

        void
        dumpQueue();

        std::string
        dumpShort();

    private:
        double playbackRate_;
        int64_t lastFrameDuration_;
        FrameBuffer::Slot::PlaybackComparator comparator_;

        void
        sort();
    };
*/

    typedef ptr_lib::shared_ptr<Slot> SlotPtr;

//    typedef
//        priority_queue< SlotPtr, vector<SlotPtr>, Slot::Comparator/*greater<Slot::Comparator>*/ >
//    PlaybackQueue;

    int count_;

    State fstate_;

    //PriorityQueue priorityQueue_;

    //Only one thread can access the buffer at the same time.
    ptr_lib::recursive_mutex syncMutex_;

    //std::vector<ptr_lib::shared_ptr<Slot> > issuedSlots_;
    typedef std::map<unsigned int, ptr_lib::shared_ptr<Slot> > map_int_slotPtr;
    std::map<unsigned int, ptr_lib::shared_ptr<Slot> > activeSlots_;
    //PlaybackQueue playbackQueue_;

    std::list<SlotPtr> suspendSlots_;
    SlotPtr playbackSlot_;

    int readySlots_count_ = 0;
    int miss_count_ = 0;
    FrameNumber lastMissNo_ = 0;

    double playbackRate_ = 30.0; // 30 frames per second (fps)

    Statistics *statistic_;

    //vector<uint8_t> *dest_; // use to transmit data in acquireFrame

    //*********************************************************
    void
    lock()
    { syncMutex_.lock(); }

    void
    unlock()
    { syncMutex_.unlock(); }

    void
    reset()
    {
        ptr_lib::lock_guard<ptr_lib::recursive_mutex> scopedLock(syncMutex_);
        playbackSlot_ = nullptr;
        readySlots_count_ = 0;
        playbackRate_ = 30.0;
        suspendSlots_.clear();
        activeSlots_.clear();
    }

    /**
     * @brief peek the first slot to decode
     * @param
     * @return first slot, if activeSlots_ is not empty
     * @note Do not consider wheather the slot has been fetched
     */
    ptr_lib::shared_ptr<Slot>
    peekSlot()
    {
        ptr_lib::shared_ptr<Slot> slot;
        slot.reset();
        if( 0 <= readySlots_count_ )
            slot = activeSlots_.begin()->second;

        return slot;
    }

    void
    suspendSlot( ptr_lib::shared_ptr<Slot> slot )
    {
        ptr_lib::lock_guard<ptr_lib::recursive_mutex> scopedLock(syncMutex_);
        suspendSlots_.push_back(slot);
        playbackSlot_ = slot;
    }

    int
    recoverSuspendedSlot(bool isRemove)
    {
        ptr_lib::lock_guard<ptr_lib::recursive_mutex> scopedLock(syncMutex_);
        ptr_lib::shared_ptr<Slot> slot;
        std::list<ptr_lib::shared_ptr<Slot> >::iterator it,tmpit;
        playbackSlot_.reset();
        it = suspendSlots_.begin();
        int n = suspendSlots_.size();
        while( it != suspendSlots_.end() )
        {
            slot = *it;
            //slot->unlock();
            // remove from activeSlots
            if( isRemove )
            {
                if( slot->getState() == Slot::StateLocked )
                    --readySlots_count_;
                VLOG(LOG_INFO) << setw(20) << setfill(' ') << std::right << getDescription()
                    << "RLSE " << slot->getPrefix()
                    << " (Tot: " << dec<< activeSlots_.size()
                    << " Rdy: " <<readySlots_count_ << ")"<< endl;
                activeSlots_.erase(slot->getFrameNo());
            }
            tmpit = it++;
            suspendSlots_.erase(tmpit);

            if( slot.use_count() > 1 )
                LOG(ERROR)
                << setw(20) << setfill(' ') << std::right << getDescription()
                << slot.use_count()
                << " slot not properly released"
                << std::endl;

            slot.reset();
        }
        suspendSlots_.clear();
        return n;
    }

    /**
     * @brief isNaluStart
     * @param slot
     * @return true if this slot is the start of a NALU or =NULL
     */
    bool
    isNaluStart(ptr_lib::shared_ptr<Slot> slot)
    {
        bool isStart = false;
        unsigned char *slotbuf;

        if( slot->getState() == Slot::StateFetched )
        {
            slotbuf = slot->getDataPtr();
            if( slotbuf == NULL )
                return true;

            if( (0 == slotbuf[0] && 0 == slotbuf[1] && 0 == slotbuf[2] && 1 == slotbuf[3])
                               || ( 0 == slotbuf[0] && 0 == slotbuf[1] && 1 == slotbuf[2]) )
            {
                isStart = true;
            }
        }
        else
            return true;
        return isStart;
    }
};


#endif /*FRAME_BUFFER_H_ */
