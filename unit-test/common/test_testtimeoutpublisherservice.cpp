// Copyright 2017 Nest Labs, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "nltest.h"
#include "errortype.hpp"

#include "test_testtimeoutpublisherservice.h"

#include "graph.hpp"
#include "detector.hpp"
#include "vertex.hpp"
#include "topicstate.hpp"
#include "testtimeoutpublisherservice.hpp"

#define SUITE_DECLARATION(name, test_ptr) { #name, test_ptr, setup_##name, teardown_##name }

using namespace DetectorGraph;

static int setup_testtimeoutpublisherservice(void *inContext)
{
    return 0;
}

static int teardown_testtimeoutpublisherservice(void *inContext)
{
    return 0;
}

static void Test_Lifetime(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    TestTimeoutPublisherService timeoutService(graph);

    bool somethingExpired = timeoutService.FireNextTimeout();
    NL_TEST_ASSERT(inSuite, somethingExpired == false);

    somethingExpired = timeoutService.FireNextTimeout();
    NL_TEST_ASSERT(inSuite, somethingExpired == false);

    somethingExpired = timeoutService.ForwardTimeAndEvaluate(100000000, graph);
    NL_TEST_ASSERT(inSuite, somethingExpired == false);
}

namespace {
    struct RecurringTestRequest : public TopicState
    {
        enum Type { Start, Stop, None };
        Type req;
        RecurringTestRequest(Type t = None) : req(t) {}
    };

    struct RecurringTestTimeout : public TopicState
    {
        int totalCount;
        RecurringTestTimeout(int v = 0) : totalCount(v) {}
    };

    struct RecurringTestDetector : public Detector,
    public SubscriberInterface<RecurringTestRequest>,

    //InternalTimer
    public TimeoutPublisher<RecurringTestTimeout>,
    public SubscriberInterface<RecurringTestTimeout>
    {
        int counter;
        RecurringTestDetector(Graph* apGraph, TimeoutPublisherService* apService) : Detector(apGraph), counter(0)
        {
            Subscribe<RecurringTestRequest>(this);

            Subscribe<RecurringTestTimeout>(this);
            SetupTimeoutPublishing<RecurringTestTimeout>(this, apService);
        }
        void Evaluate(const RecurringTestRequest& req)
        {
            if (req.req == RecurringTestRequest::Start)
            {
                TimeoutPublisher<RecurringTestTimeout>::PublishOnTimeout(RecurringTestTimeout(++counter), 10);
            }
            else if (req.req == RecurringTestRequest::Stop)
            {
                TimeoutPublisher<RecurringTestTimeout>::CancelPublishOnTimeout();
            }
        }

        void Evaluate(const RecurringTestTimeout&)
        {
            TimeoutPublisher<RecurringTestTimeout>::PublishOnTimeout(RecurringTestTimeout(++counter), 10);
        }
    };
}

static void Test_RecurringTimer(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    TestTimeoutPublisherService timeoutService(graph);
    graph.ResolveTopic<RecurringTestRequest>();
    Topic<RecurringTestTimeout>* timeoutTopic = graph.ResolveTopic<RecurringTestTimeout>();
    RecurringTestDetector detector(&graph, &timeoutService);

    RecurringTestRequest request;

    timeoutService.ForwardTimeAndEvaluate(100000, graph);
    NL_TEST_ASSERT(inSuite, !timeoutTopic->HasNewValue());

    request.req = RecurringTestRequest::Start;
    graph.PushData<RecurringTestRequest>(request);
    graph.EvaluateGraph();

    timeoutService.ForwardTimeAndEvaluate(200, graph);

    NL_TEST_ASSERT(inSuite, timeoutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, timeoutTopic->GetNewValue().totalCount == 20);

    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 100000+200);

    request.req = RecurringTestRequest::Stop;
    graph.PushData<RecurringTestRequest>(request);
    graph.EvaluateGraph();

    request.req = RecurringTestRequest::Stop;
    graph.PushData<RecurringTestRequest>(request);
    graph.EvaluateGraph();
}

namespace {
    struct ChainedTestIn : public TopicState {};
    struct ChainedTestInner : public TopicState {};
    struct ChainedTestOut : public TopicState {};

    struct ChainStartDetector : public Detector,
        public SubscriberInterface<ChainedTestIn>,
        public TimeoutPublisher<ChainedTestInner>
    {
        ChainStartDetector(Graph* apGraph, TimeoutPublisherService* apService) : Detector(apGraph)
        {
            Subscribe<ChainedTestIn>(this);
            SetupTimeoutPublishing<ChainedTestInner>(this, apService);
        }
        void Evaluate(const ChainedTestIn&)
        {
            TimeoutPublisher<ChainedTestInner>::PublishOnTimeout(ChainedTestInner(), 200);
        }
    };

    struct ChainEndDetector : public Detector,
        public SubscriberInterface<ChainedTestInner>,
        public Publisher<ChainedTestOut>
    {
        ChainEndDetector(Graph* apGraph) : Detector(apGraph)
        {
            Subscribe<ChainedTestInner>(this);
            SetupPublishing<ChainedTestOut>(this);
        }
        void Evaluate(const ChainedTestInner& req)
        {
            Publish(ChainedTestOut());
        }
    };
}

static void Test_ChainedDetectors(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    TestTimeoutPublisherService timeoutService(graph);
    graph.ResolveTopic<ChainedTestIn>();
    ChainStartDetector detectorA(&graph, &timeoutService);
    graph.ResolveTopic<ChainedTestInner>();
    ChainEndDetector detectorB(&graph);
    Topic<ChainedTestOut>* outTopic = graph.ResolveTopic<ChainedTestOut>();

    timeoutService.ForwardTimeAndEvaluate(100000, graph);
    NL_TEST_ASSERT(inSuite, !outTopic->HasNewValue());

    graph.PushData<ChainedTestIn>(ChainedTestIn());
    graph.EvaluateGraph();

    timeoutService.ForwardTimeAndEvaluate(200, graph);

    NL_TEST_ASSERT(inSuite, outTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 100000+200);
}

namespace {
    struct MetaTestTopicStateA : public TopicState {
        MetaTestTopicStateA(uint32_t aa = 0) : a(aa) {}
        uint32_t a;
    };
    struct MetaTestTopicStateB : public TopicState {
        MetaTestTopicStateB(uint32_t aa = 0) : a(aa) {}
        uint32_t a;
    };
}

static void Test_TestTimeoutPublisher(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    Topic<MetaTestTopicStateA>* outTopic = graph.ResolveTopic<MetaTestTopicStateA>();

    TestTimeoutPublisherService timeoutService(graph);
    bool somethingExpired;

#if !defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    TimeoutPublisherHandle tTimerHandleA = timeoutService.GetUniqueTimerHandle();
    MetaTestTopicStateA dataA(1234);
    timeoutService.ScheduleTimeout<MetaTestTopicStateA>(dataA, 1000, tTimerHandleA);
#endif
    TimeoutPublisherHandle tTimerHandleB = timeoutService.GetUniqueTimerHandle();
    MetaTestTopicStateA dataB(39393);
    timeoutService.ScheduleTimeout<MetaTestTopicStateA>(dataB, 100, tTimerHandleB);

    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, !outTopic->HasNewValue());

    somethingExpired = timeoutService.ForwardTimeAndEvaluate(50, graph);
    NL_TEST_ASSERT(inSuite, !outTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, somethingExpired == false);
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 50);

    somethingExpired = timeoutService.FireNextTimeout();
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, somethingExpired == true);
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 100);
    NL_TEST_ASSERT(inSuite, outTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, outTopic->GetNewValue().a == dataB.a);

#if !defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    somethingExpired = timeoutService.FireNextTimeout();
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, somethingExpired == true);
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 1000);
    NL_TEST_ASSERT(inSuite, outTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, outTopic->GetNewValue().a == dataA.a);
#endif

    somethingExpired = timeoutService.FireNextTimeout();
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, somethingExpired == false);
#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 100);
#else
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 1000);
#endif
    NL_TEST_ASSERT(inSuite, !outTopic->HasNewValue());
}

static void Test_TestTimeoutPublisher_FwdThrough(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    Topic<MetaTestTopicStateA>* outTopicA = graph.ResolveTopic<MetaTestTopicStateA>();
    Topic<MetaTestTopicStateB>* outTopicB = graph.ResolveTopic<MetaTestTopicStateB>();

    TestTimeoutPublisherService timeoutService(graph);
    bool somethingExpired;

    TimeoutPublisherHandle tTimerHandleA = timeoutService.GetUniqueTimerHandle();
    TimeoutPublisherHandle tTimerHandleB = timeoutService.GetUniqueTimerHandle();

    MetaTestTopicStateA dataA(1234);
    MetaTestTopicStateB dataB(39393);
    timeoutService.ScheduleTimeout<MetaTestTopicStateA>(dataA, 1000, tTimerHandleA);
    timeoutService.ScheduleTimeout<MetaTestTopicStateB>(dataB, 2000, tTimerHandleB);

    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, !outTopicA->HasNewValue());
    NL_TEST_ASSERT(inSuite, !outTopicB->HasNewValue());

    somethingExpired = timeoutService.ForwardTimeAndEvaluate(3000, graph);

    NL_TEST_ASSERT(inSuite, somethingExpired == true);
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 3000);
    NL_TEST_ASSERT(inSuite, !outTopicA->HasNewValue());
    NL_TEST_ASSERT(inSuite, outTopicB->HasNewValue());
    NL_TEST_ASSERT(inSuite, outTopicB->GetNewValue().a == dataB.a);
}

static void Test_TestTimeoutPublisher_Cancel(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    Topic<MetaTestTopicStateA>* outTopicA = graph.ResolveTopic<MetaTestTopicStateA>();
    Topic<MetaTestTopicStateB>* outTopicB = graph.ResolveTopic<MetaTestTopicStateB>();

    TestTimeoutPublisherService timeoutService(graph);
    bool somethingExpired;

    TimeoutPublisherHandle tTimerHandleA = timeoutService.GetUniqueTimerHandle();
    TimeoutPublisherHandle tTimerHandleB = timeoutService.GetUniqueTimerHandle();

    // Arrange
    MetaTestTopicStateA dataA(1234);
    MetaTestTopicStateB dataB(39393);
    timeoutService.ScheduleTimeout<MetaTestTopicStateA>(dataA, 2000, tTimerHandleA);
    timeoutService.ScheduleTimeout<MetaTestTopicStateB>(dataB, 3000, tTimerHandleB);

    // Act - Assert
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, !outTopicA->HasNewValue());
    NL_TEST_ASSERT(inSuite, !outTopicB->HasNewValue());

    // Act - Assert
    somethingExpired = timeoutService.ForwardTimeAndEvaluate(1000, graph);
    NL_TEST_ASSERT(inSuite, somethingExpired == false);
    NL_TEST_ASSERT(inSuite, !outTopicA->HasNewValue());
    NL_TEST_ASSERT(inSuite, !outTopicB->HasNewValue());

    // Act - Assert
    timeoutService.CancelPublishOnTimeout(tTimerHandleA);
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, somethingExpired == false);
    NL_TEST_ASSERT(inSuite, !outTopicA->HasNewValue());
    NL_TEST_ASSERT(inSuite, !outTopicB->HasNewValue());

    // Act - Assert
    somethingExpired = timeoutService.ForwardTimeAndEvaluate(2000, graph);
    NL_TEST_ASSERT(inSuite, somethingExpired == true);
    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 3000);
    NL_TEST_ASSERT(inSuite, !outTopicA->HasNewValue());
    NL_TEST_ASSERT(inSuite, outTopicB->HasNewValue());
    NL_TEST_ASSERT(inSuite, outTopicB->GetNewValue().a == dataB.a);
}

namespace {
    struct FwdToFuturePubTestIn : public TopicState {};
    struct FwdToFuturePubTestTimer : public TopicState {};
    struct FwdToFuturePubTestNormalOut : public TopicState {};
    struct FwdToFuturePubTestFutureOut : public TopicState {};

    struct FwdToFuturePubTestDetector : public Detector,
        public SubscriberInterface<FwdToFuturePubTestIn>,

        public TimeoutPublisher<FwdToFuturePubTestTimer>,
        public SubscriberInterface<FwdToFuturePubTestTimer>,

        public Publisher<FwdToFuturePubTestNormalOut>,
        public FuturePublisher<FwdToFuturePubTestFutureOut>
    {
        FwdToFuturePubTestDetector(Graph* apGraph, TimeoutPublisherService* apService) : Detector(apGraph)
        {
            Subscribe<FwdToFuturePubTestIn>(this);

            SetupTimeoutPublishing<FwdToFuturePubTestTimer>(this, apService);
            Subscribe<FwdToFuturePubTestTimer>(this);

            SetupPublishing<FwdToFuturePubTestNormalOut>(this);
            SetupFuturePublishing<FwdToFuturePubTestFutureOut>(this);
        }
        void Evaluate(const FwdToFuturePubTestIn&)
        {
            FuturePublisher<FwdToFuturePubTestFutureOut>::PublishOnFutureEvaluation(FwdToFuturePubTestFutureOut());
            TimeoutPublisher<FwdToFuturePubTestTimer>::PublishOnTimeout(FwdToFuturePubTestTimer(), 200);
        }
        void Evaluate(const FwdToFuturePubTestTimer&)
        {
            FuturePublisher<FwdToFuturePubTestFutureOut>::PublishOnFutureEvaluation(FwdToFuturePubTestFutureOut());
            Publisher<FwdToFuturePubTestNormalOut>::Publish(FwdToFuturePubTestNormalOut());
        }
    };
}

static void Test_FwdToFuturePublishDeadline(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    TestTimeoutPublisherService timeoutService(graph);
    graph.ResolveTopic<FwdToFuturePubTestIn>();
    Topic<FwdToFuturePubTestTimer>* timerTopic = graph.ResolveTopic<FwdToFuturePubTestTimer>();
    FwdToFuturePubTestDetector detectorA(&graph, &timeoutService);

    Topic<FwdToFuturePubTestNormalOut>* normalOutTopic = graph.ResolveTopic<FwdToFuturePubTestNormalOut>();
    Topic<FwdToFuturePubTestFutureOut>* futureOutTopic = graph.ResolveTopic<FwdToFuturePubTestFutureOut>();

    timeoutService.ForwardTimeAndEvaluate(100000, graph);
    NL_TEST_ASSERT(inSuite, !normalOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, !futureOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, !timerTopic->HasNewValue());

    graph.PushData<FwdToFuturePubTestIn>(FwdToFuturePubTestIn());
    graph.EvaluateGraph();

    NL_TEST_ASSERT(inSuite, !normalOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, !futureOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, !timerTopic->HasNewValue());

    timeoutService.ForwardTimeAndEvaluate(200, graph);

    NL_TEST_ASSERT(inSuite, normalOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, !futureOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, timerTopic->HasNewValue());

    graph.EvaluateGraph();

    NL_TEST_ASSERT(inSuite, !normalOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, futureOutTopic->HasNewValue());
    NL_TEST_ASSERT(inSuite, !timerTopic->HasNewValue());
}

namespace {
    struct ShortPeriodTimeout : public TopicState
    {
        static const int kPeriodMsec = 90*1000; // 90 seconds
    };

    struct MediumPeriodTimeout : public TopicState
    {
        static const int kPeriodMsec = 30*60*1000; // 30 minutes
    };

    struct LongPeriodTimeout : public TopicState
    {
        static const int kPeriodMsec = 4*60*60*1000; // 4 hours
    };

    struct PeriodicPublishingTestDetector : public Detector,
        public SubscriberInterface<ShortPeriodTimeout>,
        public SubscriberInterface<MediumPeriodTimeout>,
        public SubscriberInterface<LongPeriodTimeout>
    {
    public:
        PeriodicPublishingTestDetector(Graph* apGraph, TimeoutPublisherService* apService)
        : Detector(apGraph), shortPeriodCount(0), mediumPeriodCount(0), longPeriodCount(0)
        {
            Subscribe<ShortPeriodTimeout>(this);
            Subscribe<MediumPeriodTimeout>(this);
            Subscribe<LongPeriodTimeout>(this);

            SetupPeriodicPublishing<ShortPeriodTimeout>(ShortPeriodTimeout::kPeriodMsec, apService);
            SetupPeriodicPublishing<MediumPeriodTimeout>(MediumPeriodTimeout::kPeriodMsec, apService);
            SetupPeriodicPublishing<LongPeriodTimeout>(LongPeriodTimeout::kPeriodMsec, apService);
        }

        void Evaluate(const ShortPeriodTimeout&)
        {
            shortPeriodCount++;
        }

        void Evaluate(const MediumPeriodTimeout&)
        {
            mediumPeriodCount++;
        }

        void Evaluate(const LongPeriodTimeout&)
        {
            longPeriodCount++;
        }

        int GetShortPeriodCount()
        {
            return shortPeriodCount;
        }

        int GetMediumPeriodCount()
        {
            return mediumPeriodCount;
        }

        int GetLongPeriodCount()
        {
            return longPeriodCount;
        }

    private:
        int shortPeriodCount;
        int mediumPeriodCount;
        int longPeriodCount;
    };
}

static void Test_TestPeriodicPublishing(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    TestTimeoutPublisherService timeoutService(graph);
    graph.ResolveTopic<ShortPeriodTimeout>();
    graph.ResolveTopic<MediumPeriodTimeout>();
    graph.ResolveTopic<LongPeriodTimeout>();
    PeriodicPublishingTestDetector detector(&graph, &timeoutService);

    Topic<ShortPeriodTimeout>* shortPeriodTopic = graph.ResolveTopic<ShortPeriodTimeout>();
    Topic<MediumPeriodTimeout>* mediumPeriodTopic = graph.ResolveTopic<MediumPeriodTimeout>();
    Topic<LongPeriodTimeout>* longPeriodTopic = graph.ResolveTopic<LongPeriodTimeout>();

    timeoutService.StartPeriodicPublishing();

    timeoutService.ForwardTimeAndEvaluate(90*1000, graph);
    NL_TEST_ASSERT(inSuite, shortPeriodTopic->HasNewValue());

    timeoutService.ForwardTimeAndEvaluate(30*60*1000 - 90*1000, graph);
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, mediumPeriodTopic->HasNewValue());

    timeoutService.ForwardTimeAndEvaluate(4*60*60*1000 - 30*60*1000, graph);
    graph.EvaluateGraph();
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, longPeriodTopic->HasNewValue());

    timeoutService.ForwardTimeAndEvaluate(24*60*60*1000 - 4*60*60*1000, graph);
    graph.EvaluateGraph();
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, detector.GetShortPeriodCount() == 960);
    NL_TEST_ASSERT(inSuite, detector.GetMediumPeriodCount() == 48);
    NL_TEST_ASSERT(inSuite, detector.GetLongPeriodCount() == 6);

    timeoutService.FireNextTimeout();
    graph.EvaluateGraph();
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, detector.GetShortPeriodCount() == 961);
    NL_TEST_ASSERT(inSuite, detector.GetMediumPeriodCount() == 48);
    NL_TEST_ASSERT(inSuite, detector.GetLongPeriodCount() == 6);

    NL_TEST_ASSERT(inSuite, timeoutService.GetMetronomePeriod() == ShortPeriodTimeout::kPeriodMsec);
}

namespace {
    const int testPeriodMsec = 90*1000; // 90 seconds
    struct PeriodTimeoutA : public TopicState {};
    struct PeriodTimeoutB : public TopicState {};

    struct PeriodicTimeoutATestDetector : public Detector,
        public SubscriberInterface<PeriodTimeoutA>
    {
    public:
        PeriodicTimeoutATestDetector(Graph* apGraph, TimeoutPublisherService* apService)
        : Detector(apGraph), count(0)
        {
            Subscribe<PeriodTimeoutA>(this);
            SetupPeriodicPublishing<PeriodTimeoutA>(testPeriodMsec, apService);
        }

        void Evaluate(const PeriodTimeoutA&)
        {
            count++;
        }

        int GetPeriodCount()
        {
            return count;
        }

    private:
        int count;
    };

    struct PeriodicTimeoutBTestDetector : public Detector,
        public SubscriberInterface<PeriodTimeoutB>
    {
    public:
        PeriodicTimeoutBTestDetector(Graph* apGraph, TimeoutPublisherService* apService)
        : Detector(apGraph), count(0)
        {
            Subscribe<PeriodTimeoutB>(this);
            SetupPeriodicPublishing<PeriodTimeoutB>(testPeriodMsec, apService);
        }

        void Evaluate(const PeriodTimeoutB&)
        {
            count++;
        }

        int GetPeriodCount()
        {
            return count;
        }

    private:
        int count;
    };
}

static void Test_TestSamePeriodPublishing(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    TestTimeoutPublisherService timeoutService(graph);
    Topic<PeriodTimeoutA>* periodTopicA = graph.ResolveTopic<PeriodTimeoutA>();
    Topic<PeriodTimeoutB>* periodTopicB = graph.ResolveTopic<PeriodTimeoutB>();
    PeriodicTimeoutATestDetector detectorA(&graph, &timeoutService);
    PeriodicTimeoutBTestDetector detectorB(&graph, &timeoutService);

    timeoutService.StartPeriodicPublishing();

    timeoutService.ForwardTimeAndEvaluate(90*1000, graph);
    NL_TEST_ASSERT(inSuite, periodTopicA->HasNewValue());
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, periodTopicB->HasNewValue());

    timeoutService.ForwardTimeAndEvaluate(24*60*60*1000 - 90*1000, graph);
    graph.EvaluateGraph();
    NL_TEST_ASSERT(inSuite, detectorA.GetPeriodCount() == 960);
    NL_TEST_ASSERT(inSuite, detectorB.GetPeriodCount() == 960);
}

static void Test_WallClockOffset(nlTestSuite *inSuite, void *inContext)
{
    Graph graph;
    TestTimeoutPublisherService timeoutService(graph);

    timeoutService.SetWallClockOffset(1526186889000LL /* Sun May 13 04:48:24 UTC 2018 */);
    timeoutService.ForwardTimeAndEvaluate(24*60*60*1000, graph);

    NL_TEST_ASSERT(inSuite, timeoutService.GetTime() == 1526273289000LL /* Mon May 14 04:48:09 UTC 2018 */);
}

static const nlTest sTests[] = {
    NL_TEST_DEF("Test_Lifetime", Test_Lifetime),
    NL_TEST_DEF("Test_RecurringTimer", Test_RecurringTimer),
    NL_TEST_DEF("Test_ChainedDetectors", Test_ChainedDetectors),
    NL_TEST_DEF("Test_TestTimeoutPublisher", Test_TestTimeoutPublisher),
    NL_TEST_DEF("Test_TestTimeoutPublisher_FwdThrough", Test_TestTimeoutPublisher_FwdThrough),
    NL_TEST_DEF("Test_TestTimeoutPublisher_Cancel", Test_TestTimeoutPublisher_Cancel),
    NL_TEST_DEF("Test_FwdToFuturePublishDeadline", Test_FwdToFuturePublishDeadline),
    NL_TEST_DEF("Test_TestPeriodicPublishing", Test_TestPeriodicPublishing),
    NL_TEST_DEF("Test_TestSamePeriodPublishing", Test_TestSamePeriodPublishing),
    NL_TEST_DEF("Test_WallClockOffset", Test_WallClockOffset),
    NL_TEST_SENTINEL()
};

//This function creates the Suite (i.e: the name of your test and points to the array of test functions)
extern "C"
int testtimeoutpublisherservice_testsuite(void)
{
    nlTestSuite theSuite = SUITE_DECLARATION(testtimeoutpublisherservice, &sTests[0]);
    nlTestRunner(&theSuite, NULL);
    return nlTestRunnerStats(&theSuite);
}
