/*
 * frame-buffer.cpp
 *
 *  Created on: Jun 15, 2016
 *      Author: xyh
 */

#include <iostream>

#include "utils.h"
#include "frame-buffer.h"
#include "namespacer.h"

//*****************************************************
//  FrameBuffer::Slot::Segment
//*****************************************************
FrameBuffer::Slot::Segment::Segment()
{
    reset();
}

FrameBuffer::Slot::Segment::~Segment()
{

}

void
FrameBuffer::Slot::Segment::discard()
{
    reset();
}

void
FrameBuffer::Slot::Segment::interestIssued (const uint32_t& nonceValue)
{
    assert(nonceValue != 0);

    state_ = StatePending;

    if (requestTimeUsec_ <= 0)
        requestTimeUsec_ = NdnRtcUtils::microsecondTimestamp();

    interestNonce_ = nonceValue;
    reqCounter_++;
}

void
FrameBuffer::Slot::Segment::markMissed()
{
    state_ = StateMissing;
}

void
FrameBuffer::Slot::Segment::dataArrived (const SegmentData::SegmentMetaInfo& segmentMeta)
{
    state_ = StateFetched;
    arrivalTimeUsec_ = NdnRtcUtils::microsecondTimestamp();
    consumeTimeMs_ = segmentMeta.interestArrivalMs_;
    dataNonce_ = segmentMeta.interestNonce_;
    generationDelayMs_ = segmentMeta.generationDelayMs_;
}

bool
FrameBuffer::Slot::Segment::isOriginal()
{
    return (interestNonce_ != 0 && dataNonce_ == interestNonce_);
}

inline
SegmentData::SegmentMetaInfo
FrameBuffer::Slot::Segment::getMetadata() const
{
    SegmentData::SegmentMetaInfo meta;
    meta.generationDelayMs_ = generationDelayMs_;
    meta.interestNonce_ = interestNonce_;
    meta.interestArrivalMs_ = consumeTimeMs_;

    return meta;
}


void
FrameBuffer::Slot::Segment::reset()
{
    state_ = FrameBuffer::Slot::Segment::StateNotUsed;
    requestTimeUsec_ = -1;
    arrivalTimeUsec_ = -1;
    reqCounter_ = 0;
    dataNonce_ = 0;
    interestNonce_ = -1;
    generationDelayMs_ = -1;
    segmentNumber_ = -1;
    payloadSize_ = -1;
    consumeTimeMs_ = -1;
    prefix_ = Name();
    isParity_ = false;
}


//*****************************************************
//  FrameBuffer::Slot
//*****************************************************

FrameBuffer::Slot::Slot()
{
    reset();
}

FrameBuffer::Slot::~Slot()
{
    if (slotData_)
            free(slotData_);

    if (fecSegmentList_)
        free(fecSegmentList_);
}

void
FrameBuffer::Slot::discard()
{
    reset();
}

void
FrameBuffer::Slot::addInterest( ndn::Interest &interest )
{
    Name name = interest.getName();
    SegmentNumber segNumber;
    FrameNumber frameNumber;

    Namespacer::getSegmentationNumbers(name,frameNumber,segNumber);

    boost::shared_ptr<Segment> segment;
    segment = prepareSegment( segNumber );    // get the segment by segment number

    segment->interestIssued( NdnRtcUtils::blobToNonce(interest.getNonce()) );

    // first time to express interest for this frame
    if ( getState() == StateFree )
    {
        state_ = StateNew;
        requestTimeUsec_ = segment->getRequestTimeUsec();
        prefix_ = name;
        frameNumber_ = frameNumber;
    }

    nSegmentPending ++;
}

void
FrameBuffer::Slot::markMissed(const ndn::Interest &interest)
{
    Name name = interest.getName();
    FrameNumber frameNo;
    SegmentNumber segNo;
    Namespacer::getSegmentationNumbers(name, frameNo, segNo );
    boost::shared_ptr<Segment> segment;
    segment = getSegment(segNo);
    if( segment.get() && segment->getState() == Segment::StatePending )
    {
        segment->markMissed();
        nSegmentMissed ++;
        nSegmentPending --;
    }
}

void
FrameBuffer::Slot::appendData ( const ndn::Data &data )
{
    // set the Segment object
    boost::shared_ptr<Segment> segment;
    segment = prepareSegment( segNumber );    // get the segment by segment number
    SegmentData segmentData;
    SegmentData::segmentDataFromRaw(data.getContent().size(),
                                    data.getContent().buf(),
                                    segmentData);
    segment->dataArrived( segmentData );

    // get name component info
    Name name = data.getName();
    int frmNumber, segNumber;
    Namespacer::getSegmentationNumbers(name,frmNumber,segNumber);
    PrefixMetaInfo prefixMetaInfo;
    Namespacer::getPrefixMetaInfo(name,prefixMetaInfo);

    // update slot parameter
    State oldState = getState();
    if ( oldState == StateNew ) // first time to recieve segment in this slot
    {
        nSegmentTotal = prefixMetaInfo.totalSegmentNum_;
        // according to the first segment recieved,
        // we can know how many segment we should request
        updateSlot();
        setstate( getState() << 1 ); // change to next state (StateAssembling)
    }

    if( nSegmentReady == 1 )
        firstSegmentTimeUsec_ = segment->getArrivalTimeUsec();
    if( nSegmentReady == nSegmentTotal )
        readyTimeUsec_ = segment->getArrivalTimeUsec();
    nSegmentPending --;
    nSegmentReady ++;

    // restore ndn::Data to SegmentData
    SegmentData segmentData;
    segmentData.initFromRawData(data.getContent().size(),data.getContent().buf());
    // append segment data block to Slot::slotData_ and update segment parameter
    segment->setDataPtr(addData( segmentData, segNumber ));
    segment->setPayloadSize(data.getContent().size());
    boost::shared_ptr<Segment> segment;
    segment = getSegment( segNumber );
    if( segment->getState() == Segment::StateMissing )
        nSegmentMissed --;
    segment->dataArrived(segment.getMetaData());
    segment->setNumber(segNumber);
    Name segPrefix;
    Namespacer::getSegmentPrefix(name,segPrefix);
    segment->setPrefix(segPrefix);
}


void
FrameBuffer::Slot::reset()
{
    state_ = StateFree;
    prefix_.clear();
    frameNumber_ = -1;
    payloadSize_ = -1;

    requestTimeUsec_ = -1;
    arrivalTimeUsec_ = -1;

    nSegmentMissed = -1;
    nSegmentTotal = -1;
    nSegmentPending = -1;
    nSegmentReady = -1;

    assembledSize_ = 0;
    payloadSize_ = 0;
    //memset( slotData_,0, allocatedSize_);

    // clear all active segments if any
    std::map<SegmentNumber, boost::shared_ptr<Segment> >::iterator iter;
    for( iter = activeSegments_.begin(); iter != activeSegments_.end(); iter++ )
    {
        iter->second->discard();
        freeSegments_.push_back(iter);
        activeSegments_.erase(iter);
    }
    activeSegments_.clear();
}

/**
  * @brief pick a free segment from freeSegments_ and delete it from Slot::freeSegments_.
  * @param
  */
boost::shared_ptr<Segment>
FrameBuffer::Slot::pickFreeSegment()
{
    boost::shared_ptr<Segment> freeSegment;
    if( freeSegments_.size() )
    {
        freeSegment = freeSegments_.at(freeSegments_.size()-1);
        freeSegment = freeSegments_.pop_back();
    }
    else
    {
        freeSegment.reset(new Segment());
        //freeSegments_.push_back(freeSegment);
    }
    return freeSegment;
}

/**
  * @brief prepare a segment with segment number,
  *         this segment must in the Slot::activeSegments_,
  *         if its not in activeSegments_, then pick a free
  *         segment from freeSegments_ and set the segment number to segNo.
  * @param segNo, segment number
  */
boost::shared_ptr<Segment>
FrameBuffer::Slot::prepareSegment(SegmentNumber segNo)
{
    boost::shared_ptr<Segment> freeSegment;
    std::map<SegmentNumber, boost::shared_ptr<Segment> >::iterator iter;

    iter = activeSegments_.find(segNo);
    // segment exist in activeSegments_
    if( iter != activeSegments_.end() )
    {
        freeSegment = iter->second;
    }
    //pick a free segment and set segment number with segNo
    else
    {
        freeSegment = pickFreeSegment();
        freeSegment->setNumber(segNo);
    }
    return freeSegment;
}

/**
  * @brief get a segment by segNo from active segments.
  * @param segNo
  */
boost::shared_ptr<Segment>
FrameBuffer::Slot::getSegment(SegmentNumber segNo)
{
    boost::shared_ptr<Segment> segment;
    std::map<SegmentNumber, boost::shared_ptr<Segment> >::iterator iter;

    iter = activeSegments_.find(segNo);
    // segment exist in activeSegments_
    if( iter != activeSegments_.end() )
    {
        segment = iter->second;
    }
    return segment;
}

unsigned char*
FrameBuffer::Slot::addData( SegmentData segmentData, SegmentNumber segNo )
{
    // first time to allocate memory
    if( allocatedSize_ <= 0 )
    {
        slotData_ = (unsigned char*) malloc ( nSegmentTotal*segmentData.size() );
        allocatedSize_ = nSegmentTotal*segmentData.size();
    }
    // allocted memory is not enough
    if( allocatedSize_ - payloadSize_ < segmentData.size() )
    {
        slotData_ = (unsigned char*) realloc ( slotData_, nSegmentTotal*segmentData.size() );
        allocatedSize_ = nSegmentTotal*segmentData.size();
    }

    // copy data
    int segIdx = segNo * segmentData.size();
    memcpy( slotData_+segIdx, segmentData.getData(), segmentData.size() );
    return slotData_+segIdx;
}

void
FrameBuffer::Slot::updateSlot()
{
    if( nSegmentTotal == nSegmentPending )
        return;
    // we have express too much interest
    if( nSegmentTotal > nSegmentPending )
    {
        std::map<SegmentNumber, boost::shared_ptr<Segment>>::iterator it;
        boost::shared_ptr<Segment> segment;

        for ( it = activeSegments_.begin(); it != activeSegments_.end(); it++ )
        {
            segment = it->second;
            // the segment do not exist but we have express Interest
            // and create segment object for it, so it should be deleted
            if( segment->getNumber() > nSegmentTotal )
            {
                if( segment->getState() == Segment::StateMissing )
                    nSegmentMissed --;
                if( segment->getState() == Segment::StatePending )
                    nSegmentPending --;
                freeActiveSegment(it);
            }
        }
    }
    // segments number of this frame is more than we have expressed
    else
    {
        SegmentNumber iter;
        for ( iter = nSegmentPending; iter < nSegmentTotal; iter++ )
        callback_->onSegmentNeeded(getFrameNumber(), iter);
    }
}

void
FrameBuffer::Slot::freeActiveSegment( SegmentNumber segNo )
{
    std::vector<boost::shared_ptr<Segment>> segment;
    std::map<SegmentNumber, boost::shared_ptr<Segment>>::iterator iter;
    iter = activeSegments_.find(segNo);
    if( iter!= activeSegments_.end() )
    {
        segment = iter->second;
        segment->discard();
        freeSegments_.push_back(segment);
        activeSegments_.erase(iter);
    }
}

void
FrameBuffer::Slot::freeActiveSegment
        (std::map<SegmentNumber, boost::shared_ptr<Segment>>::iterator iter )
{
    boost::shared_ptr<Segment> segment;
    segment = iter->second;
    segment->discard();
    freeSegments_.push_back(segment);
    activeSegments_.erase(iter);
}

//*****************************************************
//  FrameBuffer
//*****************************************************

int
FrameBuffer::init()
{
    reset();
    lock_guard<recursive_mutex> scopedLock(syncMutex_);
    initialize();
}

int
FrameBuffer::reset()
{

}

void
FrameBuffer::initialize( int slotNum )
{
    while( freeSlots_.size() < slotNum )
    {
        boost::shared_ptr<Slot> slot(new Slot);
        freeSlots_.push_back(slot);
    }
}

bool
FrameBuffer::pushSlot(boost::shared_ptr<Slot> slot)
{
    std::lock_guard<std::recursive_mutex> scopedLock(syncMutex_);

    if(activeSlots_count_ >= 50)
    {
        //cout << "activeSlots_count_ " << activeSlots_count_ << endl;
        return false;
    }
    playbackQueue_.push(slot);
    activeSlots_count_++;

    return true;
}

void
FrameBuffer::recvData(const ndn::ptr_lib::shared_ptr<Data>& data)
{
    lock_guard<recursive_mutex> scopedLock(syncMutex_);
    Name name;
    Namespacer::getFramePrefix(data->getName(),name);
    FrameNumber frameNo;
    SegmentNumber segNo;
    Namespacer::getSegmentationNumbers(name,frameNo, segNo);
    //Namespacer::get
    boost::shared_ptr<Slot> slot = getSlot(name,false);
    slot->appendData(data);






    Name name = data->getName();
    int componentCount = data->getName().getComponentCount();
    FrameNumber frameNo = std::atoi(data->getName().get(componentCount-1).toEscapedString().c_str());
    map<int, boost::shared_ptr<Slot>>::iterator iter;
    iter = activeSlots_.find(frameNo);

    if ( iter == activeSlots_.end() )
    {
        cout << "FrameBuffer::dataArrived Error " << endl;
        return;
    }

    setSlot(data, iter->second);
}

void
FrameBuffer::interestTimeout(const ndn::Interest &interest)
{
    Name name = interest.getName();
    int p = Namespacer::findComponent(name,NameComponents::NameComponentStreamFrameVideo);
    if (p<0)
        p = Namespacer::findComponent(name,NameComponents::NameComponentStreamFrameAudio);
    boost::shared_ptr<Slot> slot;
    slot = getSlot(name.getSubName(0,p+2),false);
    slot->markMissed(interest);
}

void
FrameBuffer::checkMissed(FrameNumber frameNo, SegmentNumber segNo)
{
    std::map<Name, boost::shared_ptr<Slot>>::iterator iter;
    boost::shared_ptr<Slot> slot;
    for ( iter = activeSlots_.begin(); iter!=activeSlots_.end(); iter++ )
    {
        slot = iter->second;
        if( slot->getFrameNumber() < frameNo )
    }
}

boost::shared_ptr<Slot>
FrameBuffer::getSlot(const Name& prefix, bool remove)
{
    boost::shared_ptr<Slot> slot;
    std::map<Name, shared_ptr<Slot> >::iterator it;
    it = activeSlots_.find(prefix);
    if( it != activeSlots_.end() )
    {
        slot = it->second;
        if( remove )
        {
            freeSlots_.push_back(it);
            activeSlots_.erase(it);
        }
    }
}

void
FrameBuffer::setSlot(const ndn::ptr_lib::shared_ptr<Data>& data, boost::shared_ptr<FrameBuffer::Slot> slot)
{
    FrameData gotFrame;
    memcpy(&gotFrame, data->getContent().buf(), sizeof(FrameDataSt));  //copy frame header

    gotFrame.buf_ = (unsigned char*) malloc (gotFrame.header_.length_);
    memcpy( gotFrame.buf_, data->getContent().buf()+sizeof(FrameDataHeader), gotFrame.header_.length_);   //copy frame data

    /*
    for( int i = 0; i <34; i++ )
            printf("%2X ",data->getContent().buf()[i]);
    cout << endl;

    for( int i = 0; i <34; i++ )
            printf("%2X ",gotFrame.buf_[i]);
    cout << endl;
    */

    slot->lock();

    slot->setDataPtr(gotFrame.buf_);



    //slot->setNumber(std::atoi(data->getName().get(1).toEscapedString()));
    slot->setPayloadSize(gotFrame.header_.length_);
    //slot->setPrefix(data->getName());
    slot->appendData();

    slot->unlock();

    //playbackQueue_.push(slot);

}

boost::shared_ptr<FrameBuffer::Slot>
FrameBuffer::popSlot()
{
	std::lock_guard<std::recursive_mutex> scopedLock(syncMutex_);

    if ( playbackQueue_.empty() )
        return NULL;

    boost::shared_ptr<Slot>  tmp;
    tmp = playbackQueue_.top();
	//FrameBuffer::Slot*  tmp = priorityQueue_.front();

    if( tmp->getState() != FrameBuffer::Slot::StateFetched )
        return NULL;

    map<int, boost::shared_ptr<Slot> >::iterator iter;

    iter = activeSlots_.find(tmp->getFrameNumber());
    if( iter != activeSlots_.end() )
        activeSlots_.erase(iter);

    playbackQueue_.pop();
    activeSlots_count_--;

	return tmp;
}

