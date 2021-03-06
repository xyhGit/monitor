//
//  mtndn-mtndn-utils.h
//  mtndn
//
//  Copyright 2013 Regents of the University of California
//  For licensing details see the LICENSE file.
//
//  Author:  Peter Gusev
//

#ifndef _utils_
#define _utils_


#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <ndn-cpp/name.hpp>
#include <ndn-cpp/security/key-chain.hpp>

#include <sstream>

#include "params.h"

#define STR(exp) (#exp)

using namespace boost;
using namespace ndn;


class FaceProcessor;

class MtNdnUtils {

public:
    static unsigned int getSegmentsNumber(unsigned int segmentSize, unsigned int dataSize);

    static double timestamp() {
        return time(NULL)  *1000.0;
    }

    static int64_t millisecondTimestamp();
    static int64_t microsecondTimestamp();
    static int64_t nanosecondTimestamp();
    static double unixTimestamp();
    static int64_t millisecSinceEpoch();
    static int64_t microsecSinceEpoch();

    static void setIoService(boost::asio::io_service &ioService);
    static boost::asio::io_service &getIoService();
    static void startBackgroundThread();
    static int addBackgroundThread();
    static void stopBackgroundThread();
    static bool isBackgroundThread();
    static void dispatchOnBackgroundThread(boost::function<void(void)> dispatchBlock,
                                           boost::function<void(void)> onCompletion = boost::function<void(void)>());
    // synchronous version of dispatchOnBackgroundThread
    static void performOnBackgroundThread(boost::function<void(void)> dispatchBlock,
                                           boost::function<void(void)> onCompletion = boost::function<void(void)>());

    static void createLibFace(const GeneralParams &generalParams);
    static ptr_lib::shared_ptr<FaceProcessor> getLibFace();
    static void destroyLibFace();


    ///*
    static unsigned int setupFrequencyMeter(unsigned int granularity = 1);
    static void frequencyMeterTick(unsigned int meterId);
    static double currentFrequencyMeterValue(unsigned int meterId);
    static void releaseFrequencyMeter(unsigned int meterId);

    static unsigned int setupDataRateMeter(unsigned int granularity = 1);
    static void dataRateMeterMoreData(unsigned int meterId,
                                      unsigned int dataSize);
    static double currentDataRateMeterValue(unsigned int meterId);
    static void releaseDataRateMeter(unsigned int meterId);

    static unsigned int setupMeanEstimator(unsigned int sampleSize = 0,
                                           double startValue = 0.);
    static void meanEstimatorNewValue(unsigned int estimatorId, double value);
    static double currentMeanEstimation(unsigned int estimatorId);
    static double currentDeviationEstimation(unsigned int estimatorId);
    static void releaseMeanEstimator(unsigned int estimatorId);
    static void resetMeanEstimator(unsigned int estimatorId);

    static unsigned int setupSlidingAverageEstimator(unsigned int sampleSize = 2);
    static double slidingAverageEstimatorNewValue(unsigned int estimatorId, double value);
    static double currentSlidingAverageValue(unsigned int estimatorId);
    static double currentSlidingDeviationValue(unsigned int estimatorId);
    static void resetSlidingAverageEstimator(unsigned int estimatorID);
    static void releaseAverageEstimator(unsigned int estimatorID);

    static unsigned int setupFilter(double coeff = 1.);
    static void filterNewValue(unsigned int filterId, double value);
    static double currentFilteredValue(unsigned int filterId);
    static void releaseFilter(unsigned int filterId);

    static unsigned int setupInclineEstimator(unsigned int sampleSize = 0);
    static void inclineEstimatorNewValue(unsigned int estimatorId, double value);
    static double currentIncline(unsigned int estimatorId);
    static void releaseInclineEstimator(unsigned int estimatorId);

    static int frameNumber(const Name::Component &segmentComponent);
    static int segmentNumber(const Name::Component &segmentComponent);

    static int intFromComponent(const Name::Component &comp);
    static Name::Component componentFromInt(unsigned int number);

    static uint32_t generateNonceValue();
    static Blob nonceToBlob(const uint32_t nonceValue);
    static uint32_t blobToNonce(const Blob &blob);

    static void printMem(char msg[], const unsigned char *startBuf, std::size_t size );
/*
    static std::string stringFromFrameType(const WebRtcVideoFrameType &frameType);

    static unsigned int toFrames(unsigned int intervalMs,
                                 double fps);
    static unsigned int toTimeMs(unsigned int frames,
                                 double fps);
//*/
    static std::string getFullLogPath(const GeneralParams &generalParams,
                                      const std::string &fileName);

    static std::string formatString(const char *format, ...);


};

#endif /*defined(_utils_) */
